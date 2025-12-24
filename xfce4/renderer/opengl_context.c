/*
 * OpenGL context creation for AR glasses display
 * 
 * Creates an OpenGL context on the AR glasses display output
 * Uses GLX for X11-based rendering or EGL for direct DRM access
 */

#include "breezy_xfce4_renderer.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <EGL/egl.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

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

