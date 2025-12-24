/*
 * OpenGL context creation for AR glasses display
 * 
 * Creates an OpenGL context on the AR glasses display output
 * Uses GLX for X11-based rendering or EGL for direct DRM access
 */

#include "breezy_xfce4_renderer.h"
#include "logging.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <GL/gl.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <GL/glext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <drm/drm_fourcc.h>

// Fallback if headers don't define these
#ifndef DRM_FORMAT_XRGB8888
#define DRM_FORMAT_XRGB8888 0x34325258
#endif

// GLX helper functions
static int find_ar_glasses_display(Display *dpy, int screen, Window *out_window) {
    // Try to find the AR glasses display by checking RandR outputs
    // For now, we'll use the primary screen - this can be enhanced to detect AR glasses specifically
    
    Window root = RootWindow(dpy, screen);
    *out_window = root;
    
    // TODO: Use XRandR to find the AR glasses output specifically
    // For now, assume primary display
    
    return 0;
}

static int create_glx_context_on_display(RenderThread *thread, const char *display_name) {
    // Open X display
    thread->x_display = XOpenDisplay(display_name);
    if (!thread->x_display) {
        fprintf(stderr, "[GLX] Failed to open X display: %s\n", display_name ? display_name : "(default)");
        return -1;
    }
    
    int screen = DefaultScreen(thread->x_display);
    Window root = RootWindow(thread->x_display, screen);
    
    // Get visual info
    GLint attribs[] = {
        GLX_RGBA,
        GLX_DOUBLEBUFFER,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_ALPHA_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        None
    };
    
    XVisualInfo *vis = glXChooseVisual(thread->x_display, screen, attribs);
    if (!vis) {
        fprintf(stderr, "[GLX] No appropriate visual found\n");
        XCloseDisplay(thread->x_display);
        thread->x_display = NULL;
        return -1;
    }
    
    // Create colormap
    Colormap cmap = XCreateColormap(thread->x_display, root, vis->visual, AllocNone);
    
    // Create window attributes
    XSetWindowAttributes swa;
    swa.colormap = cmap;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask;
    
    // Create fullscreen window
    XWindowAttributes xwa;
    XGetWindowAttributes(thread->x_display, root, &xwa);
    
    thread->x_window = XCreateWindow(
        thread->x_display, root,
        0, 0, xwa.width, xwa.height,
        0, vis->depth, InputOutput,
        vis->visual, CWColormap | CWEventMask, &swa);
    
    // Make fullscreen (remove decorations)
    Atom wm_state = XInternAtom(thread->x_display, "_NET_WM_STATE", False);
    Atom wm_fullscreen = XInternAtom(thread->x_display, "_NET_WM_STATE_FULLSCREEN", False);
    XChangeProperty(thread->x_display, thread->x_window, wm_state, XA_ATOM, 32,
                    PropModeReplace, (unsigned char *)&wm_fullscreen, 1);
    
    XMapWindow(thread->x_display, thread->x_window);
    XFlush(thread->x_display);
    
    // Create GLX context
    thread->glx_context = glXCreateContext(thread->x_display, vis, NULL, GL_TRUE);
    if (!thread->glx_context) {
        fprintf(stderr, "[GLX] Failed to create GLX context\n");
        XDestroyWindow(thread->x_display, thread->x_window);
        XFreeColormap(thread->x_display, cmap);
        XFree(vis);
        XCloseDisplay(thread->x_display);
        thread->x_display = NULL;
        return -1;
    }
    
    // Make context current
    if (!glXMakeCurrent(thread->x_display, thread->x_window, thread->glx_context)) {
        fprintf(stderr, "[GLX] Failed to make context current\n");
        glXDestroyContext(thread->x_display, thread->glx_context);
        XDestroyWindow(thread->x_display, thread->x_window);
        XFreeColormap(thread->x_display, cmap);
        XFree(vis);
        XCloseDisplay(thread->x_display);
        thread->x_display = NULL;
        thread->glx_context = NULL;
        return -1;
    }
    
    // Enable vsync (SGI_swap_control extension)
    PFNGLXSWAPINTERVALSGIPROC glXSwapIntervalSGI = (PFNGLXSWAPINTERVALSGIPROC)
        glXGetProcAddress((const GLubyte *)"glXSwapIntervalSGI");
    if (glXSwapIntervalSGI) {
        glXSwapIntervalSGI(1);  // 1 = vsync enabled
        printf("[GLX] VSync enabled\n");
    } else {
        // Try MESA_swap_control
        PFNGLXSWAPINTERVALMESAPROC glXSwapIntervalMESA = (PFNGLXSWAPINTERVALMESAPROC)
            glXGetProcAddress((const GLubyte *)"glXSwapIntervalMESA");
        if (glXSwapIntervalMESA) {
            glXSwapIntervalMESA(1);
            printf("[GLX] VSync enabled (MESA)\n");
        } else {
            printf("[GLX] Warning: VSync extension not available\n");
        }
    }
    
    printf("[GLX] OpenGL context created successfully\n");
    printf("[GLX] OpenGL version: %s\n", glGetString(GL_VERSION));
    printf("[GLX] OpenGL vendor: %s\n", glGetString(GL_VENDOR));
    printf("[GLX] OpenGL renderer: %s\n", glGetString(GL_RENDERER));
    
    XFree(vis);
    return 0;
}

int init_opengl_context(RenderThread *thread) {
    // Try GLX first (X11-based)
    const char *display_name = getenv("DISPLAY");
    if (display_name) {
        if (create_glx_context_on_display(thread, display_name) == 0) {
            return 0;
        }
    }
    
    // TODO: Try EGL/DRM direct access as fallback
    // This would be useful for headless rendering or direct DRM access
    
    fprintf(stderr, "[OpenGL] Failed to create OpenGL context\n");
    return -1;
}

void cleanup_opengl_context(RenderThread *thread) {
    if (thread->glx_context && thread->x_display) {
        glXMakeCurrent(thread->x_display, None, NULL);
        glXDestroyContext(thread->x_display, thread->glx_context);
        thread->glx_context = NULL;
    }
    
    if (thread->x_window && thread->x_display) {
        XDestroyWindow(thread->x_display, thread->x_window);
        thread->x_window = 0;
    }
    
    if (thread->x_display) {
        XCloseDisplay(thread->x_display);
        thread->x_display = NULL;
    }
    
    if (thread->egl_display != EGL_NO_DISPLAY) {
        eglTerminate(thread->egl_display);
        thread->egl_display = EGL_NO_DISPLAY;
    }
    
    if (thread->egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(thread->egl_display, thread->egl_surface);
        thread->egl_surface = EGL_NO_SURFACE;
    }
    
    if (thread->egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(thread->egl_display, thread->egl_context);
        thread->egl_context = EGL_NO_CONTEXT;
    }
}

void swap_buffers(RenderThread *thread) {
    if (thread->glx_context && thread->x_display && thread->x_window) {
        glXSwapBuffers(thread->x_display, thread->x_window);
    } else if (thread->egl_display != EGL_NO_DISPLAY && thread->egl_surface != EGL_NO_SURFACE) {
        eglSwapBuffers(thread->egl_display, thread->egl_surface);
    }
}

// Check if EGL DMA-BUF extensions are available
static bool check_dmabuf_extensions(EGLDisplay egl_display) {
    const char *extensions = eglQueryString(egl_display, EGL_EXTENSIONS);
    if (!extensions) {
        log_error("Failed to query EGL extensions\n");
        return false;
    }
    
    bool has_dmabuf = (strstr(extensions, "EGL_EXT_image_dma_buf_import") != NULL);
    if (!has_dmabuf) {
        log_fallback("EGL DMA-BUF import", "EGL_EXT_image_dma_buf_import extension not available - zero-copy will not work!");
        log_debug("Available EGL extensions: %s\n", extensions);
    } else {
        log_debug("EGL DMA-BUF import extension available\n");
    }
    
    return has_dmabuf;
}

// Import DMA-BUF file descriptor as OpenGL texture (zero-copy)
GLuint import_dmabuf_as_texture(RenderThread *thread, int dmabuf_fd, uint32_t width, uint32_t height, uint32_t format, uint32_t stride, uint32_t modifier) {
    // For now, we need EGL display - if using GLX, we'd need to create EGL context too
    // For initial implementation, assume we're using GLX but can create EGL context if needed
    
    // Try to get EGL display from GLX
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    if (thread->glx_context && thread->x_display) {
        // We can create EGL context from same X display
        egl_display = eglGetDisplay((EGLNativeDisplayType)thread->x_display);
        if (egl_display == EGL_NO_DISPLAY) {
            fprintf(stderr, "[EGL] Failed to get EGL display from X display\n");
            return 0;
        }
        
        if (!eglInitialize(egl_display, NULL, NULL)) {
            fprintf(stderr, "[EGL] Failed to initialize EGL display\n");
            return 0;
        }
    } else if (thread->egl_display != EGL_NO_DISPLAY) {
        egl_display = thread->egl_display;
    } else {
        fprintf(stderr, "[EGL] No EGL display available\n");
        return 0;
    }
    
    // Check for DMA-BUF extensions
    if (!check_dmabuf_extensions(egl_display)) {
        fprintf(stderr, "[EGL] DMA-BUF import extension not available\n");
        return 0;
    }
    
    // Get function pointers
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC)
        eglGetProcAddress("eglCreateImageKHR");
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)
        eglGetProcAddress("eglDestroyImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
        eglGetProcAddress("glEGLImageTargetTexture2DOES");
    
    if (!eglCreateImageKHR || !eglDestroyImageKHR || !glEGLImageTargetTexture2DOES) {
        log_fallback("EGL DMA-BUF import", "Required function pointers not available (eglCreateImageKHR/eglDestroyImageKHR/glEGLImageTargetTexture2DOES)");
        if (!eglCreateImageKHR) log_debug("eglCreateImageKHR is NULL\n");
        if (!eglDestroyImageKHR) log_debug("eglDestroyImageKHR is NULL\n");
        if (!glEGLImageTargetTexture2DOES) log_debug("glEGLImageTargetTexture2DOES is NULL\n");
        return 0;
    }
    
    // Build EGL image attributes for DMA-BUF import
    EGLint attribs[32];
    int atti = 0;
    
    attribs[atti++] = EGL_WIDTH;
    attribs[atti++] = (EGLint)width;
    attribs[atti++] = EGL_HEIGHT;
    attribs[atti++] = (EGLint)height;
    attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[atti++] = (EGLint)format;
    attribs[atti++] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attribs[atti++] = dmabuf_fd;
    attribs[atti++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attribs[atti++] = 0;
    attribs[atti++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    attribs[atti++] = (EGLint)stride;
    
    // Add modifier if not linear
    if (modifier != DRM_FORMAT_MOD_LINEAR && modifier != DRM_FORMAT_MOD_INVALID) {
        attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
        attribs[atti++] = (EGLint)(modifier & 0xFFFFFFFF);
        attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
        attribs[atti++] = (EGLint)(modifier >> 32);
    }
    
    attribs[atti++] = EGL_NONE;
    
    // Create EGL image from DMA-BUF
    EGLImageKHR egl_image = eglCreateImageKHR(egl_display, EGL_NO_CONTEXT,
                                               EGL_LINUX_DMA_BUF_EXT, NULL, attribs);
    if (egl_image == EGL_NO_IMAGE_KHR) {
        EGLint error = eglGetError();
        log_error("Failed to create EGL image from DMA-BUF (error: 0x%x) - zero-copy import failed!\n", error);
        log_debug("DMA-BUF import params: width=%u, height=%u, format=0x%x, stride=%u, modifier=0x%llx\n",
                  width, height, format, stride, (unsigned long long)modifier);
        return 0;
    }
    
    log_debug("Successfully created EGL image from DMA-BUF (width=%u, height=%u, format=0x%x)\n",
              width, height, format);
    
    // Create or update OpenGL texture from EGL image
    GLuint texture = 0;
    if (thread->frame_texture == 0) {
        glGenTextures(1, &texture);
        thread->frame_texture = texture;
    } else {
        texture = thread->frame_texture;
    }
    
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Cleanup old EGL image if it exists
    if (thread->frame_egl_image != EGL_NO_IMAGE_KHR) {
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, EGL_NO_IMAGE_KHR);
        eglDestroyImageKHR(egl_display, thread->frame_egl_image, NULL);
    }
    
    // Bind EGL image to texture (zero-copy!)
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image);
    
    GLenum gl_error = glGetError();
    if (gl_error != GL_NO_ERROR) {
        log_error("Error binding EGL image to texture: 0x%x - DMA-BUF import failed!\n", gl_error);
        eglDestroyImageKHR(egl_display, egl_image, NULL);
        return 0;
    }
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    // Store EGL image for cleanup later
    thread->frame_egl_image = egl_image;
    
    log_info("DMA-BUF successfully imported as texture (zero-copy): texture=%u, %dx%d, format=0x%x, stride=%u\n",
             texture, width, height, format, stride);
    
    return texture;
}

void cleanup_dmabuf_texture(RenderThread *thread) {
    EGLDisplay egl_display = thread->egl_display;
    
    if (thread->frame_egl_image != EGL_NO_IMAGE_KHR && egl_display != EGL_NO_DISPLAY) {
        PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC)
            eglGetProcAddress("eglDestroyImageKHR");
        if (eglDestroyImageKHR) {
            eglDestroyImageKHR(egl_display, thread->frame_egl_image, NULL);
        }
        thread->frame_egl_image = EGL_NO_IMAGE_KHR;
    }
    
    if (thread->frame_texture != 0) {
        glDeleteTextures(1, &thread->frame_texture);
        thread->frame_texture = 0;
    }
    
    if (thread->current_dmabuf_fd >= 0) {
        close(thread->current_dmabuf_fd);
        thread->current_dmabuf_fd = -1;
    }
}

