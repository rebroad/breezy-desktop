#ifndef BREEZY_STANDALONE_RENDERER_H
#define BREEZY_STANDALONE_RENDERER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <xf86drmMode.h>

// Forward declare GL types if not included
#ifndef __gl_h_
typedef unsigned int GLuint;
#endif

// Forward declarations
typedef struct Renderer Renderer;
typedef struct FrameBuffer FrameBuffer;

// Capture thread structure (needed by drm_capture.c)
typedef struct CaptureThread {
    pthread_t thread;
    Renderer *renderer;
    bool running;
    bool stop_requested;
    bool thread_started;  // Track if thread was successfully started (for safe cleanup)
    
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
    
    // Cached DMA-BUF export (exported once, reused until framebuffer changes)
    int cached_dmabuf_fd;  // -1 if not exported yet
    uint32_t cached_format;
    uint32_t cached_stride;
    uint32_t cached_modifier;
} CaptureThread;

// Render thread structure (needed by opengl_context.c and shader_loader.c)
typedef struct RenderThread {
    pthread_t thread;
    Renderer *renderer;
    bool running;
    bool stop_requested;
    bool thread_started;  // Track if thread was successfully started (for safe cleanup)
    
    uint32_t refresh_rate;  // AR glasses refresh rate (60/72/90/120 Hz)
    
    // OpenGL context
    void *x_display;  // Display* (void* to avoid X11 dependency in header)
    uint32_t x_window;  // Window (uint32_t to avoid X11 dependency in header)
    void *glx_context;  // GLXContext (void* to avoid GLX dependency in header)
    void *egl_display;  // EGLDisplay (void* to avoid EGL dependency in header)
    void *egl_surface;  // EGLSurface (void* to avoid EGL dependency in header)
    void *egl_context;  // EGLContext (void* to avoid EGL dependency in header)
    
    // Shader program (from Sombrero.frag)
    uint32_t shader_program;  // GLuint (0 if not initialized)
    uint32_t vertex_shader;   // GLuint (0 if not initialized)
    uint32_t fragment_shader; // GLuint (0 if not initialized)
    
    // Texture for captured frames (DMA-BUF imported)
    uint32_t frame_texture;   // GLuint (0 if not initialized)
    void *frame_egl_image;  // EGLImageKHR (void* to avoid EGL dependency)
    
    // DMA-BUF data shared with capture thread (protected by dmabuf_mutex)
    pthread_mutex_t dmabuf_mutex;  // Protects DMA-BUF fields below
    int current_dmabuf_fd;  // -1 if not initialized
    uint32_t current_fb_id;  // Track framebuffer changes
    uint32_t current_format;  // DRM format of current framebuffer
    uint32_t current_stride;  // Stride of current framebuffer
    uint64_t current_modifier;  // Modifier of current framebuffer (uint64_t per DRM spec)
    
    // VBO/VAO for fullscreen quad
    uint32_t vbo;  // GLuint (0 if not initialized)
    uint32_t vao;  // GLuint (0 if not initialized)
} RenderThread;

// IMU data structure (must be defined before IMUReader)
typedef struct IMUData {
    float pose_orientation[16];  // 4x4 matrix: rows 0-2 are quaternions (t0, t1, t2), row 3 is timestamps
    float position[3];            // x, y, z
    uint64_t timestamp_ms;
    bool valid;
} IMUData;

// IMU reader structure (defined here so it can be used in .c files)
typedef struct IMUReader {
    int shm_fd;
    void *shm_ptr;
    size_t shm_size;
    IMUData latest;
    pthread_mutex_t lock;
} IMUReader;

// Device configuration from shared memory
typedef struct DeviceConfig {
    float look_ahead_cfg[4];
    uint32_t display_resolution[2];
    float display_fov;
    float lens_distance_ratio;
    bool sbs_enabled;
    bool custom_banner_enabled;
    bool smooth_follow_enabled;
    float smooth_follow_origin[16];  // 4x4 matrix
    bool valid;
} DeviceConfig;

// Structure to pass DMA-BUF info from capture to render thread
typedef struct {
    int dmabuf_fd;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t stride;
    uint32_t modifier;
    uint32_t fb_id;
} DmabufFrame;

// DRM capture functions (in drm_capture.c)
int init_drm_capture(CaptureThread *thread);
int export_drm_framebuffer_to_dmabuf(CaptureThread *thread, int *dmabuf_fd, uint32_t *format, uint32_t *stride, uint32_t *modifier);
void cleanup_drm_capture(CaptureThread *thread);

// IMU reader functions (in imu_reader.c)
int init_imu_reader(IMUReader *reader);
void cleanup_imu_reader(IMUReader *reader);
IMUData read_latest_imu(IMUReader *reader);
DeviceConfig read_device_config(IMUReader *reader);

// Shader loading functions (in shader_loader.c)
int load_sombrero_shaders(RenderThread *thread, const char *frag_shader_path);

// OpenGL context functions (in opengl_context.c)
int init_opengl_context(RenderThread *thread);
void cleanup_opengl_context(RenderThread *thread);
void swap_buffers(RenderThread *thread);

// DMA-BUF texture import (in opengl_context.c)
GLuint import_dmabuf_as_texture(RenderThread *thread, int dmabuf_fd, uint32_t width, uint32_t height, uint32_t format, uint32_t stride, uint64_t modifier);
void cleanup_dmabuf_texture(RenderThread *thread);

#endif

