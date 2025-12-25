/*
 * DRM/KMS capture implementation for virtual XR connector
 * 
 * Captures frames from the virtual XR connector (XR-0)
 * using direct DRM/KMS access - no X11 overhead.
 * 
 * Framebuffer ID is obtained via XRandR property (FRAMEBUFFER_ID) on XR-0 output,
 * since virtual outputs don't have KMS connectors and can't be found via DRM enumeration.
 */

#define _POSIX_C_SOURCE 200809L  // for O_CLOEXEC
#include "breezy_x11_renderer.h"
#include "logging.h"
#include <stdio.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <sys/ioctl.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xatom.h>

// drmPrimeHandleToFD is in libdrm, but we need to declare it if not available
#ifndef DRM_IOCTL_PRIME_HANDLE_TO_FD
#define DRM_IOCTL_PRIME_HANDLE_TO_FD DRM_IOWR(DRM_COMMAND_BASE + 17, struct drm_prime_handle)
#endif

// Fallback implementation if drmPrimeHandleToFD is not available
static int drm_prime_handle_to_fd(int fd, uint32_t handle, uint32_t flags, int *prime_fd) {
    struct drm_prime_handle args;
    memset(&args, 0, sizeof(args));
    args.handle = handle;
    args.flags = flags;
    
    if (ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args) < 0) {
        return -errno;
    }
    
    *prime_fd = args.fd;
    return 0;
}

#define DRM_DEVICE_PATH "/dev/dri"
#define FRAMEBUFFER_ID_PROPERTY "FRAMEBUFFER_ID"

// DRM format definitions should be in drm_fourcc.h
// Fallback definitions if header not available
#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888 0x34325258  // 'XR24' in little-endian
#endif

/**
 * Query FRAMEBUFFER_ID property from XR-0 output via XRandR
 * Returns framebuffer ID on success, 0 on failure
 */
static uint32_t query_framebuffer_id_from_randr(const char *output_name) {
    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) {
        log_error("[DRM] Failed to open X display for RandR query\n");
        return 0;
    }
    
    int event_base, error_base;
    if (!XRRQueryExtension(dpy, &event_base, &error_base)) {
        log_error("[DRM] XRandR extension not available\n");
        XCloseDisplay(dpy);
        return 0;
    }
    
    XRRScreenResources *screen_res = XRRGetScreenResources(dpy, DefaultRootWindow(dpy));
    if (!screen_res) {
        log_error("[DRM] Failed to get XRandR screen resources\n");
        XCloseDisplay(dpy);
        return 0;
    }
    
    uint32_t fb_id = 0;
    
    // Find XR-0 output
    for (int i = 0; i < screen_res->noutput; i++) {
        XRROutputInfo *output_info = XRRGetOutputInfo(dpy, screen_res, screen_res->outputs[i]);
        if (!output_info) {
            continue;
        }
        
        if (strcmp(output_info->name, output_name) == 0) {
            // Found the output, query FRAMEBUFFER_ID property
            Atom prop_atom = XInternAtom(dpy, FRAMEBUFFER_ID_PROPERTY, False);
            if (prop_atom != None) {
                Atom actual_type;
                int actual_format;
                unsigned long nitems, bytes_after;
                unsigned char *prop_data = NULL;
                
                int status = XRRGetOutputProperty(dpy, screen_res->outputs[i], prop_atom,
                                                  0, 32, False, False, AnyPropertyType,
                                                  &actual_type, &actual_format, &nitems,
                                                  &bytes_after, &prop_data);
                
                if (status == Success && prop_data && nitems == 1 && actual_format == 32) {
                    fb_id = *((uint32_t *)prop_data);
                    log_info("[DRM] Found framebuffer ID %u from %s output\n", fb_id, output_name);
                } else {
                    log_error("[DRM] Failed to read FRAMEBUFFER_ID property from %s: status=%d, nitems=%lu, format=%d\n",
                             output_name, status, nitems, actual_format);
                }
                
                if (prop_data) {
                    XFree(prop_data);
                }
            } else {
                log_error("[DRM] FRAMEBUFFER_ID atom not found\n");
            }
            
            XRRFreeOutputInfo(output_info);
            break;
        }
        
        XRRFreeOutputInfo(output_info);
    }
    
    XRRFreeScreenResources(screen_res);
    XCloseDisplay(dpy);
    
    if (fb_id == 0) {
        log_error("[DRM] Output %s not found or FRAMEBUFFER_ID property not set\n", output_name);
    }
    
    return fb_id;
}

/**
 * Try to find framebuffer in devices matching a prefix (e.g., "renderD" or "card")
 * Returns 0 on success, -1 on failure
 */
static int try_device_prefix(uint32_t fb_id, const char *prefix, char *device_path, size_t path_size) {
    DIR *dir = opendir(DRM_DEVICE_PATH);
    if (!dir) {
        return -1;
    }
    
    struct dirent *entry;
    int found = 0;
    size_t prefix_len = strlen(prefix);
    
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, prefix, prefix_len) != 0) {
            continue;
        }
        
        char device_path_tmp[512];  // Large enough for /dev/dri/ + filename
        snprintf(device_path_tmp, sizeof(device_path_tmp), "%s/%s", DRM_DEVICE_PATH, entry->d_name);
        
        int drm_fd = open(device_path_tmp, O_RDWR | O_CLOEXEC);
        if (drm_fd < 0) {
            continue;
        }
        
        // Try to get the framebuffer info
        drmModeFBPtr fb_info = drmModeGetFB(drm_fd, fb_id);
        if (fb_info) {
            // Found it!
            strncpy(device_path, device_path_tmp, path_size - 1);
            device_path[path_size - 1] = '\0';
            found = 1;
            drmModeFreeFB(fb_info);
            close(drm_fd);
            break;
        }
        
        close(drm_fd);
    }
    
    closedir(dir);
    
    return found ? 0 : -1;
}

/**
 * Find DRM device that has the given framebuffer ID
 * Tries render nodes first (more secure), then falls back to card nodes
 * Returns 0 on success, -1 on failure
 */
static int find_drm_device_for_framebuffer(uint32_t fb_id, char *device_path, size_t path_size) {
    // Try render nodes first (renderD128, renderD129, etc.) - more secure
    if (try_device_prefix(fb_id, "renderD", device_path, path_size) == 0) {
        log_info("[DRM] Using render node: %s\n", device_path);
        return 0;
    }
    
    // Fall back to card nodes (card0, card1, etc.) - works with video group
    if (try_device_prefix(fb_id, "card", device_path, path_size) == 0) {
        log_info("[DRM] Using card node: %s (render node not available)\n", device_path);
        return 0;
    }
    
    log_error("[DRM] Failed to find DRM device (renderD or card) with framebuffer ID %u\n", fb_id);
    log_error("[DRM] Make sure you are in 'video' or 'render' group\n");
    return -1;
}

// Initialize DRM capture
int init_drm_capture(CaptureThread *thread) {
    // Query framebuffer ID from XRandR property
    uint32_t fb_id = query_framebuffer_id_from_randr(thread->connector_name);
    if (fb_id == 0) {
        log_error("[DRM] Failed to get framebuffer ID from XRandR property\n");
        return -1;
    }
    
    thread->fb_id = fb_id;
    
    // Find DRM device that has this framebuffer
    char device_path[256];
    if (find_drm_device_for_framebuffer(fb_id, device_path, sizeof(device_path)) < 0) {
        log_error("[DRM] Failed to find DRM device for framebuffer ID %u\n", fb_id);
        return -1;
    }
    
    // Open DRM device
    thread->drm_fd = open(device_path, O_RDWR | O_CLOEXEC);
    if (thread->drm_fd < 0) {
        log_error("[DRM] Failed to open %s: %s\n", device_path, strerror(errno));
        return -1;
    }
    
    log_info("[DRM] Opened device: %s\n", device_path);
    
    // Get framebuffer info
    thread->fb_info = drmModeGetFB(thread->drm_fd, fb_id);
    if (!thread->fb_info) {
        log_error("[DRM] Failed to get framebuffer info for FB ID %u: %s\n", fb_id, strerror(errno));
        close(thread->drm_fd);
        thread->drm_fd = -1;
        return -1;
    }
    
    thread->width = thread->fb_info->width;
    thread->height = thread->fb_info->height;
    thread->fb_handle = thread->fb_info->handle;
    
    log_info("[DRM] Framebuffer: %dx%d, handle=%u, FB ID=%u\n",
             thread->width, thread->height, thread->fb_handle, thread->fb_id);
    
    // Note: We don't need connector_id or crtc_id for virtual outputs
    // since we're accessing the framebuffer directly
    thread->connector_id = 0;
    thread->crtc_id = 0;
    
    // Export DMA-BUF FD once during initialization (will be reused until FB changes)
    thread->cached_dmabuf_fd = -1;
    if (export_drm_framebuffer_to_dmabuf(thread, &thread->cached_dmabuf_fd,
                                          &thread->cached_format,
                                          &thread->cached_stride,
                                          &thread->cached_modifier) < 0) {
        log_error("[DRM] Failed to export DMA-BUF FD during initialization\n");
        drmModeFreeFB(thread->fb_info);
        thread->fb_info = NULL;
        close(thread->drm_fd);
        thread->drm_fd = -1;
        return -1;
    }
    
    log_info("[DRM] Exported DMA-BUF FD %d (will be reused until framebuffer changes)\n",
             thread->cached_dmabuf_fd);
    
    return 0;
}

// Export DRM framebuffer as DMA-BUF file descriptor (zero-copy)
// Returns 0 on success, -1 on error, -2 if framebuffer changed (FB ID invalidated)
int export_drm_framebuffer_to_dmabuf(CaptureThread *thread, int *dmabuf_fd, uint32_t *format, uint32_t *stride, uint32_t *modifier) {
    if (thread->drm_fd < 0 || !thread->fb_info) {
        return -1;
    }
    
    // Verify framebuffer still exists (drmModeGetFB will fail if FB was destroyed/resized)
    drmModeFBPtr fb_check = drmModeGetFB(thread->drm_fd, thread->fb_id);
    if (!fb_check) {
        // Framebuffer was destroyed - likely due to resolution change
        // Return special error code so caller can re-initialize
        log_warn("[DRM] Framebuffer ID %u no longer valid, likely due to mode change\n", thread->fb_id);
        return -2;  // FRAMEBUFFER_CHANGED
    }
    drmModeFreeFB(fb_check);
    
    // Export framebuffer handle to DMA-BUF file descriptor
    int fd = -1;
    int ret;
    
    // Try libdrm function first, fall back to ioctl if not available
    #ifdef HAVE_DRM_PRIME_HANDLE_TO_FD
    ret = drmPrimeHandleToFD(thread->drm_fd, thread->fb_info->handle, DRM_CLOEXEC | DRM_RDWR, &fd);
    #else
    log_fallback("DRM Prime export", "Using ioctl fallback instead of libdrm drmPrimeHandleToFD");
    ret = drm_prime_handle_to_fd(thread->drm_fd, thread->fb_info->handle, DRM_CLOEXEC | DRM_RDWR, &fd);
    #endif
    
    if (ret < 0 || fd < 0) {
        log_error("Failed to export DMA-BUF: %s\n", strerror(errno));
        return -1;
    }
    
    *dmabuf_fd = fd;
    
    // Get format and stride from framebuffer info
    // Note: drmModeGetFB doesn't provide format directly, we may need to query it
    // For now, assume XRGB8888 (most common format)
    if (format) {
        *format = DRM_FORMAT_XRGB8888;  // Default assumption
    }
    
    if (stride) {
        // Pitch from framebuffer info (in bytes)
        *stride = thread->fb_info->pitch;
    }
    
    if (modifier) {
        // No modifier info from drmModeGetFB - use 0 to indicate no modifier
        *modifier = 0;
    }
    
    log_debug("[DRM] Exported DMA-BUF: fd=%d, format=0x%x, stride=%u\n",
             fd, format ? *format : 0, stride ? *stride : 0);
    
    return 0;
}

// Cleanup DRM capture resources
void cleanup_drm_capture(CaptureThread *thread) {
    // Close cached DMA-BUF FD
    if (thread->cached_dmabuf_fd >= 0) {
        close(thread->cached_dmabuf_fd);
        thread->cached_dmabuf_fd = -1;
    }
    
    if (thread->fb_info) {
        drmModeFreeFB(thread->fb_info);
        thread->fb_info = NULL;
    }

    if (thread->drm_fd >= 0) {
        close(thread->drm_fd);
        thread->drm_fd = -1;
    }

    thread->fb_id = 0;
    thread->fb_handle = 0;
    thread->width = 0;
    thread->height = 0;
    thread->connector_id = 0;
    thread->crtc_id = 0;
    thread->cached_format = 0;
    thread->cached_stride = 0;
    thread->cached_modifier = 0;
}