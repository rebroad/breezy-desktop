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

#define DRM_DEVICE_PATH "/dev/dri"
#define DRM_DEVICE_PREFIX "card"

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
                                        
                                        // Map framebuffer
                                        struct drm_mode_map_dumb map_req = {
                                            .handle = thread->fb_info->handle,
                                        };
                                        if (drmIoctl(thread->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) == 0) {
                                            thread->fb_size = thread->fb_info->height * thread->fb_info->pitch;
                                            thread->fb_map = mmap(0, thread->fb_size, PROT_READ, MAP_SHARED, thread->drm_fd, map_req.offset);
                                            if (thread->fb_map == MAP_FAILED) {
                                                fprintf(stderr, "[DRM] Failed to map framebuffer: %s\n", strerror(errno));
                                                thread->fb_map = NULL;
                                            } else {
                                                printf("[DRM] Mapped framebuffer: %dx%d, pitch=%u, size=%zu\n",
                                                       thread->width, thread->height, thread->fb_info->pitch, thread->fb_size);
                                            }
                                        }
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

// Capture a frame from DRM framebuffer (CPU copy method)
// TODO: Optimize to use DMA-BUF import for zero-copy (see PIPEWIRE_VS_DRM.md)
int capture_drm_frame(CaptureThread *thread, uint8_t *output_buffer, uint32_t width, uint32_t height) {
    if (!thread->fb_map || thread->drm_fd < 0) {
        return -1;
    }
    
    // Check if framebuffer changed (page flip)
    drmModeCrtc *crtc = drmModeGetCrtc(thread->drm_fd, thread->crtc_id);
    if (!crtc) {
        return -1;
    }
    
    if (crtc->buffer_id != thread->fb_id) {
        // Framebuffer changed - remap
        if (thread->fb_info) {
            drmModeFreeFB(thread->fb_info);
        }
        if (thread->fb_map) {
            munmap(thread->fb_map, thread->fb_size);
        }
        
        thread->fb_id = crtc->buffer_id;
        thread->fb_info = drmModeGetFB(thread->drm_fd, crtc->buffer_id);
        if (thread->fb_info) {
            struct drm_mode_map_dumb map_req = {
                .handle = thread->fb_info->handle,
            };
            if (drmIoctl(thread->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req) == 0) {
                thread->fb_size = thread->fb_info->height * thread->fb_info->pitch;
                thread->fb_map = mmap(0, thread->fb_size, PROT_READ, MAP_SHARED, thread->drm_fd, map_req.offset);
            }
        }
    }
    drmModeFreeCrtc(crtc);
    
    if (!thread->fb_map || !thread->fb_info) {
        return -1;
    }
    
    // Copy framebuffer data (convert format if needed)
    // NOTE: This CPU copy can be eliminated by using DMA-BUF import (zero-copy)
    // See PIPEWIRE_VS_DRM.md for optimization details
    uint32_t pitch = thread->fb_info->pitch;
    uint8_t *src = (uint8_t *)thread->fb_map;
    
    // Assume RGB32 format - convert to RGBA if needed
    for (uint32_t y = 0; y < height && y < thread->fb_info->height; y++) {
        uint8_t *src_row = src + (y * pitch);
        uint8_t *dst_row = output_buffer + (y * width * 4);
        for (uint32_t x = 0; x < width && x < thread->fb_info->width; x++) {
            dst_row[x * 4 + 0] = src_row[x * 4 + 0]; // R
            dst_row[x * 4 + 1] = src_row[x * 4 + 1]; // G
            dst_row[x * 4 + 2] = src_row[x * 4 + 2]; // B
            dst_row[x * 4 + 3] = 255;                // A
        }
    }
    
    return 0;
}

// Cleanup DRM capture
void cleanup_drm_capture(CaptureThread *thread) {
    if (thread->fb_map) {
        munmap(thread->fb_map, thread->fb_size);
        thread->fb_map = NULL;
    }
    if (thread->fb_info) {
        drmModeFreeFB(thread->fb_info);
        thread->fb_info = NULL;
    }
    if (thread->drm_fd >= 0) {
        close(thread->drm_fd);
        thread->drm_fd = -1;
    }
}

