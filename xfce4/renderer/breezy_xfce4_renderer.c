/*
 * Breezy Desktop XFCE4 3D Renderer
 *
 * High-performance standalone renderer for XFCE4 virtual displays.
 * Uses C + OpenGL for maximum FPS - avoids Python GIL and overhead.
 *
 * Architecture:
 * - Capture thread: Reads from virtual XR connector via DRM/KMS (no X11 screen capture)
 * - Render thread: Applies GLSL shaders and renders to AR glasses display at refresh rate
 * - Lock-free ring buffer for frame transfer between threads
 * - Direct OpenGL rendering (no Qt/abstractions)
 */

#define _POSIX_C_SOURCE 200809L  // for usleep
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <dirent.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>
#include <drm/drm.h>
#include <fcntl.h>
#include <unistd.h>

#include "breezy_xfce4_renderer.h"
#include "logging.h"

// Forward declarations
typedef struct Renderer Renderer;
typedef struct FrameBuffer FrameBuffer;

// Frame buffer structure (lock-free ring buffer for maximum performance)
#define RING_BUFFER_SIZE 3  // Triple buffering

struct FrameBuffer {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    
    // Ring buffer for lock-free frame transfer
    uint8_t *frames[RING_BUFFER_SIZE];
    uint32_t write_index;  // Atomic access
    uint32_t read_index;   // Atomic access
    
    // Frame metadata
    struct timespec timestamps[RING_BUFFER_SIZE];
    uint32_t frame_count;
};

// DeviceConfig, IMUData, IMUReader, CaptureThread, RenderThread are all defined in breezy_xfce4_renderer.h

// CaptureThread and RenderThread are fully defined in breezy_xfce4_renderer.h
// Helper macros to cast void* fields to real types for use in this file
#define RT_X_DISPLAY(rt) ((Display*)(rt)->x_display)
#define RT_GLX_CONTEXT(rt) ((GLXContext)(rt)->glx_context)
#define RT_EGL_DISPLAY(rt) ((EGLDisplay)(rt)->egl_display)
#define RT_EGL_SURFACE(rt) ((EGLSurface)(rt)->egl_surface)
#define RT_EGL_CONTEXT(rt) ((EGLContext)(rt)->egl_context)
#define RT_EGL_IMAGE(rt) ((EGLImageKHR)(rt)->frame_egl_image)

// Accessor macros for void* EGL fields  
#define SET_EGL_IMAGE(rt, val) ((rt)->frame_egl_image = (void*)(val))

// Main renderer structure
struct Renderer {
    FrameBuffer frame_buffer;
    IMUReader imu_reader;
    CaptureThread capture_thread;
    RenderThread render_thread;
    
    // Configuration
    uint32_t virtual_width;
    uint32_t virtual_height;
    uint32_t virtual_framerate;
    uint32_t render_refresh_rate;
    
    // Device configuration (cached, updated periodically)
    DeviceConfig device_config;
    uint64_t last_config_update_ms;
    
    // Control
    bool running;
    pthread_mutex_t control_lock;
};

// Function prototypes
// IMU reader functions are declared in breezy_xfce4_renderer.h (not static)

static void *capture_thread_func(void *arg);
static int init_capture_thread(CaptureThread *thread, Renderer *renderer);
static void cleanup_capture_thread(CaptureThread *thread);

static void *render_thread_func(void *arg);
static int init_render_thread(RenderThread *thread, Renderer *renderer);
static void cleanup_render_thread(RenderThread *thread);
static int load_shaders(RenderThread *thread);
static void render_frame(RenderThread *thread, FrameBuffer *fb, IMUData *imu, DeviceConfig *config);

static int init_frame_buffer(FrameBuffer *fb, uint32_t width, uint32_t height);
static void cleanup_frame_buffer(FrameBuffer *fb);
static bool write_frame(FrameBuffer *fb, const uint8_t *data, uint32_t width, uint32_t height);
static bool read_latest_frame(FrameBuffer *fb, uint8_t **data, struct timespec *timestamp);


// Frame Buffer Implementation (Lock-free ring buffer)
static int init_frame_buffer(FrameBuffer *fb, uint32_t width, uint32_t height) {
    memset(fb, 0, sizeof(*fb));
    fb->width = width;
    fb->height = height;
    fb->stride = width * 4;  // RGBA
    
    size_t frame_size = fb->stride * height;
    
    for (int i = 0; i < RING_BUFFER_SIZE; i++) {
        fb->frames[i] = malloc(frame_size);
        if (!fb->frames[i]) {
            log_error("Failed to allocate frame buffer %d\n", i);
            for (int j = 0; j < i; j++) {
                free(fb->frames[j]);
            }
            return -1;
        }
        clock_gettime(CLOCK_MONOTONIC, &fb->timestamps[i]);
    }
    
    return 0;
}

static void cleanup_frame_buffer(FrameBuffer *fb) {
    // With DMA-BUF, no pixel data to free
    // Just clear pointers
    for (int i = 0; i < RING_BUFFER_SIZE; i++) {
        fb->frames[i] = NULL;
    }
}

static bool write_frame(FrameBuffer *fb, const uint8_t *data, uint32_t width, uint32_t height) {
    if (width != fb->width || height != fb->height) {
        return false;
    }
    
    // Lock-free write: advance write index
    uint32_t next_write = (fb->write_index + 1) % RING_BUFFER_SIZE;
    
    // With DMA-BUF, we don't copy pixel data here
    // data can be NULL - this is just a marker that a new frame is available
    // The actual frame is accessed via DMA-BUF in the render thread
    
    // Update timestamp
    clock_gettime(CLOCK_MONOTONIC, &fb->timestamps[next_write]);
    
    // Atomic update of write index (capture thread only writes)
    __sync_synchronize();  // Memory barrier
    fb->write_index = next_write;
    fb->frame_count++;
    
    return true;
}

static bool read_latest_frame(FrameBuffer *fb, uint8_t **data, struct timespec *timestamp) {
    // Lock-free read: read from current write index
    __sync_synchronize();  // Memory barrier
    uint32_t read_idx = fb->write_index;
    
    // With DMA-BUF, frames[read_idx] is NULL (we don't store pixel data)
    // We just use this to detect new frames via timestamp
    
    if (timestamp) {
        *timestamp = fb->timestamps[read_idx];
    }
    
    // Return true if we have a valid timestamp (new frame available)
    // data is set to NULL - render thread uses DMA-BUF instead
    if (data) {
        *data = NULL;  // No pixel data pointer with DMA-BUF
    }
    
    return true;  // Always return true if write_index is valid
}

// Capture Thread Implementation
static void *capture_thread_func(void *arg) {
    CaptureThread *thread = (CaptureThread *)arg;
    
    log_info("[Capture] Thread started for %dx%d@%dHz\n",
             thread->width, thread->height, thread->framerate);
    
    const double frame_time = 1.0 / thread->framerate;
    struct timespec next_frame_time;
    clock_gettime(CLOCK_MONOTONIC, &next_frame_time);
    
    while (!thread->stop_requested) {
        // Export DRM framebuffer as DMA-BUF (zero-copy)
        int dmabuf_fd = -1;
        uint32_t format, stride, modifier;
        
        if (export_drm_framebuffer_to_dmabuf(thread, &dmabuf_fd, &format, &stride, &modifier) == 0) {
            RenderThread *render_thread = &thread->renderer->render_thread;
            
            // Lock mutex to protect shared DMA-BUF data
            pthread_mutex_lock(&render_thread->dmabuf_mutex);
            
            // Close old fd if framebuffer changed
            if (render_thread->current_dmabuf_fd >= 0 && 
                thread->fb_id != render_thread->current_fb_id) {
                close(render_thread->current_dmabuf_fd);
            }
            
            // Pass DMA-BUF info to render thread
            render_thread->current_dmabuf_fd = dmabuf_fd;
            render_thread->current_fb_id = thread->fb_id;
            render_thread->current_format = format;
            render_thread->current_stride = stride;
            render_thread->current_modifier = modifier;
            
            pthread_mutex_unlock(&render_thread->dmabuf_mutex);
            
            // Signal new frame available (marker frame - no pixel copy)
            uint8_t *dummy = NULL;  // No pixel data - render thread uses DMA-BUF
            write_frame(&thread->renderer->frame_buffer, dummy, thread->width, thread->height);
        } else {
            // Export failed, wait a bit
            usleep(10000);  // 10ms
        }
        
        // Sleep until next frame
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        
        double elapsed = (now.tv_sec - next_frame_time.tv_sec) +
                        (now.tv_nsec - next_frame_time.tv_nsec) / 1e9;
        
        if (elapsed < frame_time) {
            struct timespec sleep_time = {
                .tv_sec = 0,
                .tv_nsec = (long)((frame_time - elapsed) * 1e9)
            };
            nanosleep(&sleep_time, NULL);
        }
        
        // Update next frame time
        next_frame_time.tv_sec += (time_t)frame_time;
        next_frame_time.tv_nsec += (long)((frame_time - (int)frame_time) * 1e9);
        if (next_frame_time.tv_nsec >= 1000000000L) {
            next_frame_time.tv_sec++;
            next_frame_time.tv_nsec -= 1000000000L;
        }
    }
    
    log_info("[Capture] Thread stopping\n");
    return NULL;
}

static int init_capture_thread(CaptureThread *thread, Renderer *renderer) {
    memset(thread, 0, sizeof(*thread));
    thread->renderer = renderer;
    thread->running = false;
    thread->stop_requested = false;
    thread->thread_started = false;
    thread->drm_fd = -1;
    thread->connector_name = "XR-0";  // Default virtual connector name
    
    // Initialize DRM capture
    if (init_drm_capture(thread) < 0) {
        log_error("[Capture] Failed to initialize DRM capture\n");
        return -1;
    }
    
    return 0;
}

static void cleanup_capture_thread(CaptureThread *thread) {
    thread->stop_requested = true;
    // Only join if thread was actually started (prevents double-join)
    if (thread->thread_started && thread->running) {
        pthread_join(thread->thread, NULL);
        thread->thread_started = false;
        thread->running = false;
    }
    cleanup_drm_capture(thread);
}

// Render Thread Implementation
static void *render_thread_func(void *arg) {
    RenderThread *thread = (RenderThread *)arg;
    
    log_info("[Render] Thread started at %dHz\n", thread->refresh_rate);
    
    const double frame_time = 1.0 / thread->refresh_rate;
    struct timespec next_frame_time;
    clock_gettime(CLOCK_MONOTONIC, &next_frame_time);
    
    while (!thread->stop_requested) {
        // Read latest frame from ring buffer
        uint8_t *frame_data = NULL;
        struct timespec frame_timestamp;
        if (!read_latest_frame(&thread->renderer->frame_buffer,
                              &frame_data,
                              &frame_timestamp)) {
            // No frame available, skip this render
            usleep(1000);  // 1ms
            continue;
        }
        
        // Read latest IMU data
        IMUData imu = read_latest_imu(&thread->renderer->imu_reader);
        
        // Update device config periodically (every second)
        uint64_t current_time_ms = 0;
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
            current_time_ms = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
        }
        
        if (thread->renderer->last_config_update_ms == 0 || 
            current_time_ms - thread->renderer->last_config_update_ms > 1000) {
            thread->renderer->device_config = read_device_config(&thread->renderer->imu_reader);
            thread->renderer->last_config_update_ms = current_time_ms;
        }
        
        // Render frame with 3D transformations
        render_frame(thread, &thread->renderer->frame_buffer, &imu, &thread->renderer->device_config);
        
        // Swap buffers (vsync)
        swap_buffers(thread);
        
        // Sleep until next frame
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        
        double elapsed = (now.tv_sec - next_frame_time.tv_sec) +
                        (now.tv_nsec - next_frame_time.tv_nsec) / 1e9;
        
        if (elapsed < frame_time) {
            struct timespec sleep_time = {
                .tv_sec = 0,
                .tv_nsec = (long)((frame_time - elapsed) * 1e9)
            };
            nanosleep(&sleep_time, NULL);
        }
        
        // Update next frame time
        next_frame_time.tv_sec += (time_t)frame_time;
        next_frame_time.tv_nsec += (long)((frame_time - (int)frame_time) * 1e9);
        if (next_frame_time.tv_nsec >= 1000000000L) {
            next_frame_time.tv_sec++;
            next_frame_time.tv_nsec -= 1000000000L;
        }
    }
    
    log_info("[Render] Thread stopping\n");
    return NULL;
}

static int create_fullscreen_quad(GLuint *vbo, GLuint *vao) {
    // Fullscreen quad vertices (NDC coordinates -1 to 1)
    float vertices[] = {
        // Positions (x, y)    // Texture coordinates (u, v)
        -1.0f, -1.0f,          0.0f, 0.0f,  // Bottom-left
         1.0f, -1.0f,          1.0f, 0.0f,  // Bottom-right
        -1.0f,  1.0f,          0.0f, 1.0f,  // Top-left
         1.0f,  1.0f,          1.0f, 1.0f   // Top-right
    };
    
    glGenVertexArrays(1, vao);
    glGenBuffers(1, vbo);
    
    glBindVertexArray(*vao);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    // Position attribute (location 0)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    
    // Texture coordinate attribute (location 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);
    
    return 0;
}

static int init_render_thread(RenderThread *thread, Renderer *renderer) {
    memset(thread, 0, sizeof(*thread));
    thread->renderer = renderer;
    thread->running = false;
    thread->stop_requested = false;
    thread->thread_started = false;
    thread->x_display = NULL;
    thread->glx_context = NULL;
    thread->egl_display = EGL_NO_DISPLAY;
    thread->egl_surface = EGL_NO_SURFACE;
    thread->egl_context = EGL_NO_CONTEXT;
    thread->shader_program = 0;
    thread->vertex_shader = 0;
    thread->fragment_shader = 0;
    thread->frame_texture = 0;
    SET_EGL_IMAGE(thread, EGL_NO_IMAGE_KHR);
    thread->current_dmabuf_fd = -1;
    thread->current_fb_id = 0;
    thread->current_format = 0;
    thread->current_stride = 0;
    thread->current_modifier = 0;
    thread->vbo = 0;
    thread->vao = 0;
    
    // Initialize mutex for DMA-BUF data sharing
    if (pthread_mutex_init(&thread->dmabuf_mutex, NULL) != 0) {
        log_error("[Render] Failed to initialize DMA-BUF mutex\n");
        return -1;
    }
    
    // Create OpenGL context on AR glasses display
    if (init_opengl_context(thread) != 0) {
        log_error("[Render] Failed to create OpenGL context\n");
        return -1;
    }
    
    // Load and compile GLSL shaders from Sombrero.frag
    if (load_shaders(thread) != 0) {
        log_error("[Render] Failed to load shaders\n");
        cleanup_opengl_context(thread);
        return -1;
    }
    
    // Create fullscreen quad VBO/VAO
    if (create_fullscreen_quad(&thread->vbo, &thread->vao) != 0) {
        log_error("[Render] Failed to create fullscreen quad\n");
        cleanup_opengl_context(thread);
        return -1;
    }
    
    log_info("[Render] Render thread initialized successfully\n");
    return 0;
}

static void cleanup_render_thread(RenderThread *thread) {
    thread->stop_requested = true;
    // Only join if thread was actually started (prevents double-join)
    if (thread->thread_started && thread->running) {
        pthread_join(thread->thread, NULL);
        thread->thread_started = false;
        thread->running = false;
    }
    
    // Destroy mutex
    pthread_mutex_destroy(&thread->dmabuf_mutex);
    
    // Cleanup OpenGL resources
    if (thread->shader_program) {
        glDeleteProgram(thread->shader_program);
        thread->shader_program = 0;
    }
    if (thread->vertex_shader) {
        glDeleteShader(thread->vertex_shader);
        thread->vertex_shader = 0;
    }
    if (thread->fragment_shader) {
        glDeleteShader(thread->fragment_shader);
        thread->fragment_shader = 0;
    }
    cleanup_dmabuf_texture(thread);
    if (thread->vbo) {
        glDeleteBuffers(1, &thread->vbo);
        thread->vbo = 0;
    }
    if (thread->vao) {
        glDeleteVertexArrays(1, &thread->vao);
        thread->vao = 0;
    }
    
    cleanup_opengl_context(thread);
}

static int load_shaders(RenderThread *thread) {
    // Try to load from multiple possible paths
    const char *possible_paths[] = {
        "../modules/sombrero/Sombrero.frag",
        "../../modules/sombrero/Sombrero.frag",
        "/usr/share/breezy-desktop/shaders/Sombrero.frag",
        NULL
    };
    
    const char *frag_path = NULL;
    for (int i = 0; possible_paths[i]; i++) {
        FILE *f = fopen(possible_paths[i], "r");
        if (f) {
            fclose(f);
            frag_path = possible_paths[i];
            break;
        }
    }
    
    if (!frag_path) {
        log_error("[Shader] Sombrero.frag not found in any standard location\n");
        return -1;
    }
    
    return load_sombrero_shaders(thread, frag_path);
}

// Helper function to set shader uniforms
static void set_shader_uniforms(RenderThread *thread, IMUData *imu, DeviceConfig *config, uint32_t width, uint32_t height) {
    if (!thread->shader_program || !imu->valid || !config->valid) {
        return;
    }
    
    // Calculate look_ahead_ms: data age + constant (or use override if provided)
    uint64_t current_time_ms = 0;
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        current_time_ms = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
    }
    uint64_t data_age_ms = (current_time_ms > imu->timestamp_ms) ? (current_time_ms - imu->timestamp_ms) : 0;
    float look_ahead_ms = config->look_ahead_cfg[0] + (float)data_age_ms;
    
    // Calculate frametime (inverse of refresh rate)
    float frametime = 1000.0f / (float)thread->refresh_rate;
    
    // Calculate FOV values from display_fov
    float display_aspect_ratio = (float)config->display_resolution[0] / (float)config->display_resolution[1];
    float diag_to_vert_ratio = sqrtf(display_aspect_ratio * display_aspect_ratio + 1.0f);
    float half_fov_z_rads = (config->display_fov * M_PI / 180.0f) / diag_to_vert_ratio / 2.0f;
    float half_fov_y_rads = half_fov_z_rads * display_aspect_ratio;
    float fov_half_widths[2] = {tanf(half_fov_y_rads), tanf(half_fov_z_rads)};
    float fov_widths[2] = {fov_half_widths[0] * 2.0f, fov_half_widths[1] * 2.0f};
    
    // Calculate source_to_display_ratio
    float source_to_display_ratio[2] = {
        (float)width / (float)config->display_resolution[0],
        (float)height / (float)config->display_resolution[1]
    };
    
    // Set uniforms
    GLint loc;
    
    // Basic uniforms
    if ((loc = glGetUniformLocation(thread->shader_program, "virtual_display_enabled")) >= 0) {
        glUniform1i(loc, 1);  // Always enabled in renderer
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "pose_orientation")) >= 0) {
        glUniformMatrix4fv(loc, 1, GL_FALSE, imu->pose_orientation);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "pose_position")) >= 0) {
        glUniform3fv(loc, 1, imu->position);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "look_ahead_cfg")) >= 0) {
        glUniform4fv(loc, 1, config->look_ahead_cfg);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "display_resolution")) >= 0) {
        glUniform2f(loc, (float)config->display_resolution[0], (float)config->display_resolution[1]);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "source_to_display_ratio")) >= 0) {
        glUniform2fv(loc, 1, source_to_display_ratio);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "display_size")) >= 0) {
        glUniform1f(loc, 1.0f);  // Full screen
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "display_north_offset")) >= 0) {
        glUniform1f(loc, 1.0f);  // Default distance
    }
    
    // Lens vectors (from lens_distance_ratio)
    float lens_vector[3] = {config->lens_distance_ratio, 0.0f, 0.0f};
    if ((loc = glGetUniformLocation(thread->shader_program, "lens_vector")) >= 0) {
        glUniform3fv(loc, 1, lens_vector);
    }
    if ((loc = glGetUniformLocation(thread->shader_program, "lens_vector_r")) >= 0) {
        glUniform3fv(loc, 1, lens_vector);  // Same for both eyes initially
    }
    
    // Texture coordinate limits (full screen)
    float texcoord_x_limits[2] = {0.0f, 1.0f};
    if ((loc = glGetUniformLocation(thread->shader_program, "texcoord_x_limits")) >= 0) {
        glUniform2fv(loc, 1, texcoord_x_limits);
    }
    if ((loc = glGetUniformLocation(thread->shader_program, "texcoord_x_limits_r")) >= 0) {
        glUniform2fv(loc, 1, texcoord_x_limits);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "show_banner")) >= 0) {
        glUniform1i(loc, 0);  // No banner by default
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "frametime")) >= 0) {
        glUniform1f(loc, frametime);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "look_ahead_ms")) >= 0) {
        glUniform1f(loc, look_ahead_ms);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "custom_banner_enabled")) >= 0) {
        glUniform1i(loc, config->custom_banner_enabled ? 1 : 0);
    }
    
    float trim_percent[2] = {0.0f, 0.0f};
    if ((loc = glGetUniformLocation(thread->shader_program, "trim_percent")) >= 0) {
        glUniform2fv(loc, 1, trim_percent);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "curved_display")) >= 0) {
        glUniform1i(loc, 0);  // Flat display by default
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "sbs_enabled")) >= 0) {
        glUniform1i(loc, config->sbs_enabled ? 1 : 0);
    }
    
    // FOV uniforms
    if ((loc = glGetUniformLocation(thread->shader_program, "half_fov_z_rads")) >= 0) {
        glUniform1f(loc, half_fov_z_rads);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "half_fov_y_rads")) >= 0) {
        glUniform1f(loc, half_fov_y_rads);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "fov_half_widths")) >= 0) {
        glUniform2fv(loc, 1, fov_half_widths);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "fov_widths")) >= 0) {
        glUniform2fv(loc, 1, fov_widths);
    }
    
    // Sideview uniforms
    if ((loc = glGetUniformLocation(thread->shader_program, "sideview_enabled")) >= 0) {
        glUniform1i(loc, 0);  // Disabled by default
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "sideview_position")) >= 0) {
        glUniform1f(loc, 0.0f);
    }
    
    // Other uniforms
    float banner_position[2] = {0.5f, 0.9f};
    if ((loc = glGetUniformLocation(thread->shader_program, "banner_position")) >= 0) {
        glUniform2fv(loc, 1, banner_position);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "day_in_seconds")) >= 0) {
        glUniform1f(loc, 24.0f * 60.0f * 60.0f);
    }
    
    // Date and keepalive (simplified - could be enhanced)
    float date[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if ((loc = glGetUniformLocation(thread->shader_program, "date")) >= 0) {
        glUniform4fv(loc, 1, date);
    }
    if ((loc = glGetUniformLocation(thread->shader_program, "keepalive_date")) >= 0) {
        glUniform4fv(loc, 1, date);
    }
    
    float imu_reset_data[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if ((loc = glGetUniformLocation(thread->shader_program, "imu_reset_data")) >= 0) {
        glUniform4fv(loc, 1, imu_reset_data);
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "look_ahead_ms_cap")) >= 0) {
        glUniform1f(loc, 45.0f);  // Default cap
    }
    
    if ((loc = glGetUniformLocation(thread->shader_program, "sbs_mode_stretched")) >= 0) {
        glUniform1i(loc, 0);  // Not stretched by default
    }
}

static void render_frame(RenderThread *thread, FrameBuffer *fb, IMUData *imu, DeviceConfig *config) {
    if (!thread->shader_program || !thread->vao) {
        return;
    }
    
    // Get frame dimensions
    int width = fb->width;
    int height = fb->height;
    
    // Check if we have a new DMA-BUF to import (framebuffer changed)
    // Lock mutex to read shared DMA-BUF data
    pthread_mutex_lock(&thread->dmabuf_mutex);
    
    int dmabuf_fd = thread->current_dmabuf_fd;
    uint32_t fb_id = thread->current_fb_id;
    uint32_t format = thread->current_format;
    uint32_t stride = thread->current_stride;
    uint32_t modifier = thread->current_modifier;
    
    // Mark as consumed immediately (capture thread will provide new one if needed)
    if (dmabuf_fd >= 0) {
        thread->current_dmabuf_fd = -1;
    }
    
    pthread_mutex_unlock(&thread->dmabuf_mutex);
    
    if (dmabuf_fd >= 0) {
        // Import DMA-BUF as texture (zero-copy)
        GLuint texture = import_dmabuf_as_texture(thread, dmabuf_fd,
                                                   width, height, format, stride, modifier);
        if (texture == 0) {
            log_error("Failed to import DMA-BUF as texture - rendering will be skipped\n");
            // Close fd on failure
            close(dmabuf_fd);
            return;
        }
        
        // fd ownership transferred to EGL image - don't close it here
        // It will be closed when EGL image is destroyed
    }
    
    if (thread->frame_texture == 0) {
        // No texture yet, skip rendering
        return;
    }
    
    // Read latest frame marker (for timestamp)
    uint8_t *frame_data = NULL;
    struct timespec frame_timestamp;
    if (!read_latest_frame(fb, &frame_data, &frame_timestamp)) {
        return;
    }
    
    // Clear screen
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Use shader program
    glUseProgram(thread->shader_program);
    
    // Bind VAO and texture
    glBindVertexArray(thread->vao);
    
    // Set shader uniforms
    set_shader_uniforms(thread, imu, config, width, height);
    
    // Set screen texture
    GLint screen_tex_loc = glGetUniformLocation(thread->shader_program, "screenTexture");
    if (screen_tex_loc >= 0) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, thread->frame_texture);
        glUniform1i(screen_tex_loc, 0);
    }
    
    // Note: frame_texture now directly references DRM framebuffer via DMA-BUF (zero-copy!)
    
    // Render fullscreen quad
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    glBindVertexArray(0);
    glUseProgram(0);
}

// Main renderer
static Renderer *g_renderer = NULL;

static void signal_handler(int sig) {
    (void)sig;
    if (g_renderer) {
        g_renderer->running = false;
    }
}

int main(int argc, char *argv[]) {
    // Initialize logging first
    if (log_init() != 0) {
        fprintf(stderr, "Warning: Failed to initialize logging, continuing with stderr output\n");
    }
    
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <width> <height> <capture_fps> <render_fps>\n", argv[0]);
        fprintf(stderr, "Example: %s 1920 1080 60 90\n", argv[0]);
        log_error("Invalid arguments: expected 4 arguments, got %d\n", argc - 1);
        log_cleanup();
        return 1;
    }
    
    Renderer renderer = {0};
    g_renderer = &renderer;
    
    renderer.virtual_width = atoi(argv[1]);
    renderer.virtual_height = atoi(argv[2]);
    renderer.virtual_framerate = atoi(argv[3]);
    renderer.render_refresh_rate = atoi(argv[4]);
    
    log_info("Breezy XFCE4 Renderer starting\n");
    log_info("Virtual display: %dx%d@%dHz\n",
             renderer.virtual_width,
             renderer.virtual_height,
             renderer.virtual_framerate);
    log_info("Render rate: %dHz\n", renderer.render_refresh_rate);
    
    // Initialize components
    if (init_frame_buffer(&renderer.frame_buffer,
                         renderer.virtual_width,
                         renderer.virtual_height) != 0) {
        log_error("Failed to initialize frame buffer\n");
        return 1;
    }
    
    if (init_imu_reader(&renderer.imu_reader) != 0) {
        log_error("Failed to initialize IMU reader\n");
        cleanup_frame_buffer(&renderer.frame_buffer);
        return 1;
    }
    
    if (init_capture_thread(&renderer.capture_thread, &renderer) != 0) {
        log_error("Failed to initialize capture thread\n");
        cleanup_imu_reader(&renderer.imu_reader);
        cleanup_frame_buffer(&renderer.frame_buffer);
        return 1;
    }
    
    if (init_render_thread(&renderer.render_thread, &renderer) != 0) {
        log_error("Failed to initialize render thread\n");
        cleanup_capture_thread(&renderer.capture_thread);
        cleanup_imu_reader(&renderer.imu_reader);
        cleanup_frame_buffer(&renderer.frame_buffer);
        return 1;
    }
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Start threads
    renderer.running = true;
    renderer.capture_thread.running = true;
    renderer.capture_thread.stop_requested = false;
    renderer.render_thread.running = true;
    renderer.render_thread.stop_requested = false;
    
    if (pthread_create(&renderer.capture_thread.thread, NULL,
                      capture_thread_func, &renderer.capture_thread) != 0) {
        log_error("Failed to create capture thread\n");
        goto cleanup;
    }
    renderer.capture_thread.thread_started = true;
    
    if (pthread_create(&renderer.render_thread.thread, NULL,
                      render_thread_func, &renderer.render_thread) != 0) {
        log_error("Failed to create render thread\n");
        // Stop capture thread and let cleanup handle joining
        renderer.capture_thread.stop_requested = true;
        goto cleanup;
    }
    renderer.render_thread.thread_started = true;
    
    log_info("Renderer running. Press Ctrl+C to stop.\n");
    
    // Main loop
    while (renderer.running) {
        sleep(1);
    }
    
cleanup:
    log_info("Shutting down renderer\n");
    
    // Stop threads - cleanup functions will handle joining safely
    renderer.capture_thread.stop_requested = true;
    renderer.render_thread.stop_requested = true;
    
    // Cleanup (will join threads if they were started)
    cleanup_render_thread(&renderer.render_thread);
    cleanup_capture_thread(&renderer.capture_thread);
    cleanup_imu_reader(&renderer.imu_reader);
    cleanup_frame_buffer(&renderer.frame_buffer);
    
    log_cleanup();
    return 0;
}
