# DMA-BUF Zero-Copy Optimization

## Current Implementation (CPU Copy)

The current implementation maps the DRM framebuffer and copies pixels to the OpenGL texture buffer:

```c
// Map framebuffer
mmap(DRM framebuffer) -> mapped memory

// Copy pixels (CPU overhead)
memcpy(mapped memory -> texture buffer)
```

**Latency**: ~1-2ms CPU copy overhead
**CPU Usage**: Moderate (copying ~8MB per frame at 1920x1080)

## Zero-Copy Optimization: DMA-BUF Import

We can eliminate the CPU copy by using **DMA-BUF import** - the same technique Mutter uses for zero-copy compositing.

### How It Works

1. **Export DRM framebuffer as DMA-BUF**:
   ```c
   int dmabuf_fd;
   drmPrimeHandleToFD(drm_fd, fb_info->handle, DRM_CLOEXEC, &dmabuf_fd);
   ```

2. **Create EGL image from DMA-BUF**:
   ```c
   EGLint attribs[] = {
       EGL_WIDTH, width,
       EGL_HEIGHT, height,
       EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_XRGB8888,  // Format
       EGL_DMA_BUF_PLANE0_FD_EXT, dmabuf_fd,
       EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
       EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
       EGL_NONE
   };
   EGLImageKHR egl_image = eglCreateImageKHR(
       egl_display, EGL_NO_CONTEXT,
       EGL_LINUX_DMA_BUF_EXT, NULL, attribs
   );
   ```

3. **Create OpenGL texture from EGL image** (zero-copy):
   ```c
   GLuint texture;
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image);
   ```

**Result**: The texture directly references the DRM framebuffer memory - no copy!

### Benefits

- ✅ **Zero CPU copy**: GPU-to-GPU transfer only
- ✅ **Lower latency**: < 0.5ms (vs 1-2ms with CPU copy)
- ✅ **Lower CPU usage**: No pixel copying overhead
- ✅ **Hardware-accelerated**: Uses GPU memory directly

### Requirements

**EGL Extensions:**
- `EGL_EXT_image_dma_buf_import`
- `EGL_EXT_image_dma_buf_import_modifiers` (for format negotiation)

**OpenGL Extensions:**
- `GL_OES_EGL_image` or `GL_EXT_EGL_image_storage`

**DRM Support:**
- Framebuffer must support DMA-BUF export (most modern drivers do)
- Check with: `drmPrimeHandleToFD()` - should succeed

### Implementation Notes

1. **Frame Updates**: When the framebuffer changes (page flip), we need to:
   - Export new DMA-BUF fd
   - Create new EGL image
   - Update texture binding

2. **Format Detection**: Query framebuffer format via DRM properties:
   ```c
   drmModeObjectGetProperties() -> DRM_FORMAT property
   ```

3. **Stride/Pitch**: Use framebuffer's actual pitch, not `width * bpp`

4. **Error Handling**: Fall back to CPU copy if DMA-BUF import fails

### Code Structure

```c
// In CaptureThread structure
int dmabuf_fd;           // Current DMA-BUF file descriptor
EGLImageKHR egl_image;   // Current EGL image

// In capture thread
int export_drm_framebuffer_to_dmabuf(CaptureThread *thread) {
    // Export handle to DMA-BUF fd
    return drmPrimeHandleToFD(thread->drm_fd,
                              thread->fb_info->handle,
                              DRM_CLOEXEC,
                              &thread->dmabuf_fd);
}

// In render thread (OpenGL context)
GLuint import_dmabuf_as_texture(RenderThread *thread, int dmabuf_fd) {
    // Create EGL image from DMA-BUF
    // Create texture from EGL image
    // Return texture ID
}
```

### Testing

1. **Check EGL extensions**:
   ```c
   const char *extensions = eglQueryString(egl_display, EGL_EXTENSIONS);
   // Check for EGL_EXT_image_dma_buf_import
   ```

2. **Test DMA-BUF export**:
   ```c
   int fd;
   int ret = drmPrimeHandleToFD(drm_fd, handle, DRM_CLOEXEC, &fd);
   // Should succeed on modern drivers
   ```

3. **Compare performance**: Measure latency with CPU copy vs DMA-BUF

### Migration Path

**Phase 1** (Current): CPU copy implementation
- Works on all systems
- Acceptable performance for testing
- Simpler to debug

**Phase 2** (Optimization): DMA-BUF import
- Check for extensions at runtime
- Try DMA-BUF import first
- Fall back to CPU copy if not available
- Result: Zero-copy when supported, CPU copy otherwise

This gives us the best of both worlds - zero-copy performance when available, compatibility when not.

