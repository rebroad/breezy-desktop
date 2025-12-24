#ifndef BREEZY_XFCE4_RENDERER_H
#define BREEZY_XFCE4_RENDERER_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <xf86drmMode.h>

// Forward declarations
typedef struct Renderer Renderer;
typedef struct CaptureThread CaptureThread;
typedef struct RenderThread RenderThread;
typedef struct FrameBuffer FrameBuffer;
typedef struct IMUReader IMUReader;
typedef struct IMUData IMUData;
typedef struct DeviceConfig DeviceConfig;

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
GLuint import_dmabuf_as_texture(RenderThread *thread, int dmabuf_fd, uint32_t width, uint32_t height, uint32_t format, uint32_t stride, uint32_t modifier);
void cleanup_dmabuf_texture(RenderThread *thread);

#endif

