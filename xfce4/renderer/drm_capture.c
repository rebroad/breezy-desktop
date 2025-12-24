/*
 * DRM/KMS capture implementation for virtual XR connector
 * 
 * Finds and captures frames from the virtual XR connector (XR-0)
 * using direct DRM/KMS access - no X11 overhead.
 */

#include "breezy_xfce4_renderer.h"
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
#define DRM_DEVICE_PREFIX "card"

// DRM format definitions should be in drm_fourcc.h
// Fallback definitions if header not available
#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888 0x34325258  // 'XR24' in little-endian
#endif

// Find DRM device that has our virtual connector
static int find_drm_device_with_xr_connector(const char *connector_name, char *device_path, size_t path_size) {
    DIR *dir;
    struct dirent *entry;
    int drm_fd = -1;
    int found = 0;
    
    dir = opendir(DRM_DEVICE_PATH);
    if (!dir) {
        fprintf(stderr, "[DRM] Failed to open %s: %s\n", DRM_DEVICE_PATH, strerror(errno));
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, DRM_DEVICE_PREFIX, strlen(DRM_DEVICE_PREFIX)) != 0) {
            continue;
        }
        
        char device_path_tmp[256];
        snprintf(device_path_tmp, sizeof(device_path_tmp), "%s/%s", DRM_DEVICE_PATH, entry->d_name);
        
        drm_fd = open(device_path_tmp, O_RDWR | O_CLOEXEC);
        if (drm_fd < 0) {
            continue;
        }
        
        // Query connectors
        drmModeRes *resources = drmModeGetResources(drm_fd);
        if (!resources) {
            close(drm_fd);
            continue;
        }
        
        // Search for virtual XR connector
        for (int i = 0; i < resources->count_connectors; i++) {
            drmModeConnector *connector = drmModeGetConnector(drm_fd, resources->connectors[i]);
            if (!connector) {
                continue;
            }
            
            // Get connector name
            drmModeObjectProperties *props = drmModeObjectGetProperties(drm_fd, resources->connectors[i], DRM_MODE_OBJECT_CONNECTOR);
            if (props) {
                for (uint32_t j = 0; j < props->count_props; j++) {
                    drmModePropertyRes *prop = drmModeGetProperty(drm_fd, props->props[j]);
                    if (prop && strcmp(prop->name, "NAME") == 0) {
                        const char *name = (const char *)props->values[j];
                        if (strcmp(name, connector_name) == 0) {
                            // Found it!
                            strncpy(device_path, device_path_tmp, path_size - 1);
                            device_path[path_size - 1] = '\0';
                            found = 1;
                            drmModeFreeProperty(prop);
                            break;
                        }
                    }
                    if (prop) drmModeFreeProperty(prop);
                }
                drmModeFreeObjectProperties(props);
            }
            
            drmModeFreeConnector(connector);
            if (found) break;
        }
        
        drmModeFreeResources(resources);
        
        if (found) {
            close(drm_fd);
            break;
        }
        close(drm_fd);
    }
    
    closedir(dir);
    return found ? 0 : -1;
}

// Initialize DRM capture
int init_drm_capture(CaptureThread *thread) {
    char device_path[256];
    
    // Find DRM device with virtual XR connector
    if (find_drm_device_with_xr_connector(thread->connector_name, device_path, sizeof(device_path)) < 0) {
        fprintf(stderr, "[DRM] Failed to find DRM device with connector %s\n", thread->connector_name);
        return -1;
    }
    
    // Open DRM device
    thread->drm_fd = open(device_path, O_RDWR | O_CLOEXEC);
    if (thread->drm_fd < 0) {
        fprintf(stderr, "[DRM] Failed to open %s: %s\n", device_path, strerror(errno));
        return -1;
    }
    
    printf("[DRM] Opened device: %s\n", device_path);
    
    // Get resources
    drmModeRes *resources = drmModeGetResources(thread->drm_fd);
    if (!resources) {
        fprintf(stderr, "[DRM] Failed to get DRM resources\n");
        close(thread->drm_fd);
        thread->drm_fd = -1;
        return -1;
    }
    
    // Find connector and CRTC
    for (int i = 0; i < resources->count_connectors; i++) {
        drmModeConnector *connector = drmModeGetConnector(thread->drm_fd, resources->connectors[i]);
        if (!connector) continue;
        
        drmModeObjectProperties *props = drmModeObjectGetProperties(thread->drm_fd, resources->connectors[i], DRM_MODE_OBJECT_CONNECTOR);
        if (props) {
            for (uint32_t j = 0; j < props->count_props; j++) {
                drmModePropertyRes *prop = drmModeGetProperty(thread->drm_fd, props->props[j]);
                if (prop && strcmp(prop->name, "NAME") == 0) {
                    const char *name = (const char *)props->values[j];
                    if (strcmp(name, thread->connector_name) == 0) {
                        thread->connector_id = resources->connectors[i];
                        if (connector->encoder_id) {
                            drmModeEncoder *encoder = drmModeGetEncoder(thread->drm_fd, connector->encoder_id);
                            if (encoder && encoder->crtc_id) {
                                thread->crtc_id = encoder->crtc_id;
                                
                                // Get current framebuffer
                                drmModeCrtc *crtc = drmModeGetCrtc(thread->drm_fd, encoder->crtc_id);
                                if (crtc && crtc->buffer_id) {
                                    thread->fb_id = crtc->buffer_id;
                                    thread->fb_info = drmModeGetFB(thread->drm_fd, crtc->buffer_id);
                                    if (thread->fb_info) {
                                        thread->width = thread->fb_info->width;
                                        thread->height = thread->fb_info->height;
                                        thread->fb_handle = thread->fb_info->handle;
                                        
                                        printf("[DRM] Found framebuffer: %dx%d, handle=%u\n",
                                               thread->width, thread->height, thread->fb_handle);
                                    }
                                }
                                drmModeFreeCrtc(crtc);
                            }
                            drmModeFreeEncoder(encoder);
                        }
                    }
                }
                if (prop) drmModeFreeProperty(prop);
            }
            drmModeFreeObjectProperties(props);
        }
        drmModeFreeConnector(connector);
    }
    
    drmModeFreeResources(resources);
    
    if (thread->connector_id == 0 || thread->crtc_id == 0) {
        fprintf(stderr, "[DRM] Failed to find connector/CRTC for %s\n", thread->connector_name);
        close(thread->drm_fd);
        thread->drm_fd = -1;
        return -1;
    }
    
    return 0;
}

// Export DRM framebuffer as DMA-BUF file descriptor (zero-copy)
int export_drm_framebuffer_to_dmabuf(CaptureThread *thread, int *dmabuf_fd, uint32_t *format, uint32_t *stride, uint32_t *modifier) {
    if (thread->drm_fd < 0 || !thread->fb_info) {
        return -1;
    }
    
    // Check if framebuffer changed (page flip)
    drmModeCrtc *crtc = drmModeGetCrtc(thread->drm_fd, thread->crtc_id);
    if (!crtc) {
        return -1;
    }
    
    if (crtc->buffer_id != thread->fb_id) {
        // Framebuffer changed - update info
        if (thread->fb_info) {
            drmModeFreeFB(thread->fb_info);
        }
        
        thread->fb_id = crtc->buffer_id;
        thread->fb_info = drmModeGetFB(thread->drm_fd, crtc->buffer_id);
        if (thread->fb_info) {
            thread->fb_handle = thread->fb_info->handle;
            thread->width = thread->fb_info->width;
            thread->height = thread->fb_info->height;
        }
    }
    drmModeFreeCrtc(crtc);
    
    if (!thread->fb_info) {
        return -1;
    }
    
    // Export framebuffer handle to DMA-BUF file descriptor
    int fd = -1;
    int ret;
    
    // Try libdrm function first, fall back to ioctl if not available
    #ifdef HAVE_DRM_PRIME_HANDLE_TO_FD
    ret = drmPrimeHandleToFD(thread->drm_fd, thread->fb_info->handle, DRM_CLOEXEC | DRM_RDWR, &fd);
    #else
    ret = drm_prime_handle_to_fd(thread->drm_fd, thread->fb_info->handle, DRM_CLOEXEC | DRM_RDWR, &fd);
    #endif
    
    if (ret < 0 || fd < 0) {
        fprintf(stderr, "[DRM] Failed to export DMA-BUF: %s\n", strerror(errno));
        return -1;
    }
    
    *dmabuf_fd = fd;
    *stride = thread->fb_info->pitch;
    
    // Try to get format from framebuffer properties
    // Query DRM object properties for format and modifier
    drmModeObjectProperties *props = drmModeObjectGetProperties(thread->drm_fd, thread->fb_id, DRM_MODE_OBJECT_FB);
    if (props) {
        for (uint32_t i = 0; i < props->count_props; i++) {
            drmModePropertyRes *prop = drmModeGetProperty(thread->drm_fd, props->props[i]);
            if (prop) {
                if (strcmp(prop->name, "FB_ID") == 0) {
                    // Already have this
                } else if (strcmp(prop->name, "IN_FMT_FOURCC") == 0 || strcmp(prop->name, "IN_FORMAT") == 0) {
                    *format = (uint32_t)props->prop_values[i];
                } else if (strcmp(prop->name, "IN_FMT_MODIFIER") == 0 || strcmp(prop->name, "IN_MODIFIER") == 0) {
                    *modifier = (uint32_t)props->prop_values[i];
                }
                drmModeFreeProperty(prop);
            }
        }
        drmModeFreeObjectProperties(props);
    }
    
    // Default to XRGB8888 if format not found
    if (*format == 0) {
        *format = DRM_FORMAT_XRGB8888;
    }
    
    // Default to linear modifier if not found
    if (*modifier == 0 || *modifier == DRM_FORMAT_MOD_INVALID) {
        *modifier = DRM_FORMAT_MOD_LINEAR;
    }
    
    return 0;
}

// Cleanup DRM capture
void cleanup_drm_capture(CaptureThread *thread) {
    if (thread->fb_info) {
        drmModeFreeFB(thread->fb_info);
        thread->fb_info = NULL;
    }
    if (thread->drm_fd >= 0) {
        close(thread->drm_fd);
        thread->drm_fd = -1;
    }
}

