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

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>

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
    void *fb_map;  // Mapped framebuffer memory
    size_t fb_size;  // Framebuffer size in bytes
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

// IMU Reader Implementation
static int init_imu_reader(IMUReader *reader) {
    memset(reader, 0, sizeof(*reader));
    reader->shm_fd = -1;
    reader->latest.valid = false;
    
    if (pthread_mutex_init(&reader->lock, NULL) != 0) {
        fprintf(stderr, "Failed to initialize IMU reader mutex\n");
        return -1;
    }
    
    // TODO: Open /dev/shm/breezy_desktop_imu and map it
    // See GNOME extension's devicedatastream.js for binary format reference
    
    return 0;
}

static void cleanup_imu_reader(IMUReader *reader) {
    if (reader->shm_ptr && reader->shm_fd != -1) {
        munmap(reader->shm_ptr, reader->shm_size);
        close(reader->shm_fd);
    }
    pthread_mutex_destroy(&reader->lock);
}

static IMUData read_latest_imu(IMUReader *reader) {
    IMUData result;
    pthread_mutex_lock(&reader->lock);
    result = reader->latest;
    pthread_mutex_unlock(&reader->lock);
    return result;
}

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
    for (int i = 0; i < RING_BUFFER_SIZE; i++) {
        if (fb->frames[i]) {
            free(fb->frames[i]);
            fb->frames[i] = NULL;
        }
    }
}

static bool write_frame(FrameBuffer *fb, const uint8_t *data, uint32_t width, uint32_t height) {
    if (width != fb->width || height != fb->height) {
        return false;
    }
    
    // Lock-free write: advance write index
    uint32_t next_write = (fb->write_index + 1) % RING_BUFFER_SIZE;
    
    // Copy frame data
    size_t frame_size = fb->stride * height;
    memcpy(fb->frames[next_write], data, frame_size);
    
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
    
    if (!fb->frames[read_idx]) {
        return false;
    }
    
    *data = fb->frames[read_idx];
    if (timestamp) {
        *timestamp = fb->timestamps[read_idx];
    }
    
    return true;
}

// Capture Thread Implementation
static void *capture_thread_func(void *arg) {
    CaptureThread *thread = (CaptureThread *)arg;
    
    printf("[Capture] Thread started for %dx%d@%dHz\n",
           thread->width, thread->height, thread->framerate);
    
    const double frame_time = 1.0 / thread->framerate;
    struct timespec next_frame_time;
    clock_gettime(CLOCK_MONOTONIC, &next_frame_time);
    
    // Allocate capture buffer
    size_t frame_size = thread->width * thread->height * 4;
    uint8_t *capture_buffer = malloc(frame_size);
    if (!capture_buffer) {
        fprintf(stderr, "[Capture] Failed to allocate capture buffer\n");
        return NULL;
    }
    
    while (!thread->stop_requested) {
        // Capture from virtual XR connector via DRM/KMS
        if (capture_drm_frame(thread, capture_buffer, thread->width, thread->height) == 0) {
            // Write to ring buffer
            write_frame(&thread->renderer->frame_buffer,
                       capture_buffer,
                       thread->width,
                       thread->height);
        } else {
            // Capture failed, wait a bit
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
    
    free(capture_buffer);
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
        // TODO: Use EGL swap_buffers with vsync
        
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
    
    // TODO: Create OpenGL context on AR glasses display
    // 1. Open X display
    // 2. Create GLX context or EGL context
    // 3. Make current
    
    // TODO: Load and compile GLSL shaders from Sombrero.frag
    if (load_shaders(thread) != 0) {
        fprintf(stderr, "Failed to load shaders\n");
        return -1;
    }
    
    // TODO: Create fullscreen quad VBO/VAO
    
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
    }
    if (thread->frame_texture) {
        glDeleteTextures(1, &thread->frame_texture);
    }
    
    // TODO: Destroy OpenGL context
}

static int load_shaders(RenderThread *thread) {
    // TODO: Load vertex and fragment shaders from Sombrero.frag
    // Port GLSL code from modules/sombrero/Sombrero.frag
    
    // Placeholder
    printf("[Render] Shaders not yet implemented\n");
    return -1;
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
