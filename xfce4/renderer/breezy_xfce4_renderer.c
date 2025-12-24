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
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <GL/gl.h>
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

// Forward declarations
typedef struct Renderer Renderer;
typedef struct CaptureThread CaptureThread;
typedef struct RenderThread RenderThread;
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

// IMU data structure
typedef struct {
    float quaternion[4];  // w, x, y, z
    float position[3];    // x, y, z
    uint64_t timestamp_ms;
    bool valid;
} IMUData;

// IMU reader
typedef struct {
    int shm_fd;
    void *shm_ptr;
    size_t shm_size;
    IMUData latest;
    pthread_mutex_t lock;
} IMUReader;

// Capture thread
struct CaptureThread {
    pthread_t thread;
    Renderer *renderer;
    bool running;
    bool stop_requested;
    
    // Virtual XR connector properties
    const char *connector_name;  // e.g., "XR-0"
    uint32_t width;
    uint32_t height;
    uint32_t framerate;
    
    // DRM/KMS capture
    int drm_fd;  // -1 if not initialized
    uint32_t connector_id;
    uint32_t crtc_id;
    uint32_t fb_id;  // Current framebuffer ID
    drmModeFBPtr fb_info;  // Current framebuffer info
    uint32_t fb_handle;  // Framebuffer handle for DMA-BUF export
};

// Render thread
struct RenderThread {
    pthread_t thread;
    Renderer *renderer;
    bool running;
    bool stop_requested;
    
    uint32_t refresh_rate;  // AR glasses refresh rate (60/72/90/120 Hz)
    
    // OpenGL context
    Display *x_display;  // NULL if not initialized
    Window x_window;
    GLXContext glx_context;  // NULL if not initialized
    EGLDisplay egl_display;  // EGL_NO_DISPLAY if not initialized
    EGLSurface egl_surface;  // EGL_NO_SURFACE if not initialized
    EGLContext egl_context;  // EGL_NO_CONTEXT if not initialized
    
    // Shader program (from Sombrero.frag)
    GLuint shader_program;  // 0 if not initialized
    GLuint vertex_shader;   // 0 if not initialized
    GLuint fragment_shader; // 0 if not initialized
    
    // Texture for captured frames
    GLuint frame_texture;   // 0 if not initialized
    
    // VBO/VAO for fullscreen quad
    GLuint vbo;  // 0 if not initialized
    GLuint vao;  // 0 if not initialized
};

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
    
    // Control
    bool running;
    pthread_mutex_t control_lock;
};

// Function prototypes
static int init_imu_reader(IMUReader *reader);
static void cleanup_imu_reader(IMUReader *reader);
static IMUData read_latest_imu(IMUReader *reader);

static void *capture_thread_func(void *arg);
static int init_capture_thread(CaptureThread *thread, Renderer *renderer);
static void cleanup_capture_thread(CaptureThread *thread);

static void *render_thread_func(void *arg);
static int init_render_thread(RenderThread *thread, Renderer *renderer);
static void cleanup_render_thread(RenderThread *thread);
static int load_shaders(RenderThread *thread);
static void render_frame(RenderThread *thread, FrameBuffer *fb, IMUData *imu);

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
            fprintf(stderr, "Failed to allocate frame buffer %d\n", i);
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
    
    printf("[Capture] Thread started for %dx%d@%dHz\n",
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
    
    printf("[Capture] Thread stopping\n");
    return NULL;
}

static int init_capture_thread(CaptureThread *thread, Renderer *renderer) {
    memset(thread, 0, sizeof(*thread));
    thread->renderer = renderer;
    thread->running = false;
    thread->stop_requested = false;
    thread->drm_fd = -1;
    thread->connector_name = "XR-0";  // Default virtual connector name
    
    // Initialize DRM capture
    if (init_drm_capture(thread) < 0) {
        fprintf(stderr, "[Capture] Failed to initialize DRM capture\n");
        return -1;
    }
    
    return 0;
}

static void cleanup_capture_thread(CaptureThread *thread) {
    thread->stop_requested = true;
    if (thread->running) {
        pthread_join(thread->thread, NULL);
    }
    cleanup_drm_capture(thread);
}

// Render Thread Implementation
static void *render_thread_func(void *arg) {
    RenderThread *thread = (RenderThread *)arg;
    
    printf("[Render] Thread started at %dHz\n", thread->refresh_rate);
    
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
        
        // Render frame with 3D transformations
        render_frame(thread, &thread->renderer->frame_buffer, &imu);
        
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
    
    printf("[Render] Thread stopping\n");
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
    thread->x_display = NULL;
    thread->glx_context = NULL;
    thread->egl_display = EGL_NO_DISPLAY;
    thread->egl_surface = EGL_NO_SURFACE;
    thread->egl_context = EGL_NO_CONTEXT;
    thread->shader_program = 0;
    thread->vertex_shader = 0;
    thread->fragment_shader = 0;
    thread->frame_texture = 0;
    thread->vbo = 0;
    thread->vao = 0;
    
    // Create OpenGL context on AR glasses display
    if (init_opengl_context(thread) != 0) {
        fprintf(stderr, "[Render] Failed to create OpenGL context\n");
        return -1;
    }
    
    // Load and compile GLSL shaders from Sombrero.frag
    if (load_shaders(thread) != 0) {
        fprintf(stderr, "[Render] Failed to load shaders\n");
        cleanup_opengl_context(thread);
        return -1;
    }
    
    // Create fullscreen quad VBO/VAO
    if (create_fullscreen_quad(&thread->vbo, &thread->vao) != 0) {
        fprintf(stderr, "[Render] Failed to create fullscreen quad\n");
        cleanup_opengl_context(thread);
        return -1;
    }
    
    printf("[Render] Render thread initialized successfully\n");
    return 0;
}

static void cleanup_render_thread(RenderThread *thread) {
    thread->stop_requested = true;
    if (thread->running) {
        pthread_join(thread->thread, NULL);
    }
    
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
        fprintf(stderr, "[Shader] Sombrero.frag not found in any standard location\n");
        return -1;
    }
    
    return load_sombrero_shaders(thread, frag_path);
}

static void render_frame(RenderThread *thread, FrameBuffer *fb, IMUData *imu) {
    // TODO: Apply 3D transformations using IMU data and GLSL shader
    // 1. Upload frame to texture
    // 2. Set shader uniforms (IMU quaternion, position, etc.)
    // 3. Render fullscreen quad with shader
    
    // Placeholder
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
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <width> <height> <capture_fps> <render_fps>\n", argv[0]);
        fprintf(stderr, "Example: %s 1920 1080 60 90\n", argv[0]);
        return 1;
    }
    
    Renderer renderer = {0};
    g_renderer = &renderer;
    
    renderer.virtual_width = atoi(argv[1]);
    renderer.virtual_height = atoi(argv[2]);
    renderer.virtual_framerate = atoi(argv[3]);
    renderer.render_refresh_rate = atoi(argv[4]);
    
    printf("Breezy XFCE4 Renderer\n");
    printf("Virtual display: %dx%d@%dHz\n",
           renderer.virtual_width,
           renderer.virtual_height,
           renderer.virtual_framerate);
    printf("Render rate: %dHz\n", renderer.render_refresh_rate);
    
    // Initialize components
    if (init_frame_buffer(&renderer.frame_buffer,
                         renderer.virtual_width,
                         renderer.virtual_height) != 0) {
        fprintf(stderr, "Failed to initialize frame buffer\n");
        return 1;
    }
    
    if (init_imu_reader(&renderer.imu_reader) != 0) {
        fprintf(stderr, "Failed to initialize IMU reader\n");
        cleanup_frame_buffer(&renderer.frame_buffer);
        return 1;
    }
    
    if (init_capture_thread(&renderer.capture_thread, &renderer) != 0) {
        fprintf(stderr, "Failed to initialize capture thread\n");
        cleanup_imu_reader(&renderer.imu_reader);
        cleanup_frame_buffer(&renderer.frame_buffer);
        return 1;
    }
    
    if (init_render_thread(&renderer.render_thread, &renderer) != 0) {
        fprintf(stderr, "Failed to initialize render thread\n");
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
        fprintf(stderr, "Failed to create capture thread\n");
        goto cleanup;
    }
    
    if (pthread_create(&renderer.render_thread.thread, NULL,
                      render_thread_func, &renderer.render_thread) != 0) {
        fprintf(stderr, "Failed to create render thread\n");
        renderer.capture_thread.stop_requested = true;
        pthread_join(renderer.capture_thread.thread, NULL);
        goto cleanup;
    }
    
    printf("Renderer running. Press Ctrl+C to stop.\n");
    
    // Main loop
    while (renderer.running) {
        sleep(1);
    }
    
cleanup:
    // Stop threads
    renderer.capture_thread.stop_requested = true;
    renderer.render_thread.stop_requested = true;
    
    pthread_join(renderer.capture_thread.thread, NULL);
    pthread_join(renderer.render_thread.thread, NULL);
    
    // Cleanup
    cleanup_render_thread(&renderer.render_thread);
    cleanup_capture_thread(&renderer.capture_thread);
    cleanup_imu_reader(&renderer.imu_reader);
    cleanup_frame_buffer(&renderer.frame_buffer);
    
    printf("Renderer stopped.\n");
    return 0;
}
