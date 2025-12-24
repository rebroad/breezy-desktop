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

// DRM capture functions (in drm_capture.c)
int init_drm_capture(CaptureThread *thread);
int capture_drm_frame(CaptureThread *thread, uint8_t *output_buffer, uint32_t width, uint32_t height);
void cleanup_drm_capture(CaptureThread *thread);

// Shader loading functions (in shader_loader.c)
int load_sombrero_shaders(RenderThread *thread, const char *frag_shader_path);

#endif

