# PipeWire vs DRM/KMS for Virtual Display Capture

## PipeWire via ScreenCast (GNOME/Mutter Approach)

### How It Works:
1. **Application Request**: Breezy requests a virtual display via D-Bus API:
   ```python
   session.RecordVirtual({'is-platform': True})
   ```

2. **Mutter Creates Virtual Display**: Mutter's compositor creates an off-screen buffer and a PipeWire stream

3. **PipeWire Stream**: Mutter exposes the virtual display content via a PipeWire stream (low-level video streaming protocol)

4. **Application Receives**: Breezy connects to the PipeWire stream using GStreamer:
   ```python
   pipewiresrc path=%u ! video/x-raw,width=%d,height=%d
   ```

5. **Zero-Copy When Possible**: PipeWire uses DMA-BUF (Direct Memory Access Buffer) for hardware-accelerated, zero-copy transfer when supported

### Performance Characteristics:
- ✅ **Hardware Accelerated**: Uses DMA-BUF for zero-copy when GPU supports it
- ✅ **Format Negotiation**: PipeWire handles format conversion automatically
- ✅ **Well-Optimized**: Mature implementation with many optimizations
- ⚠️ **Protocol Overhead**: D-Bus + PipeWire protocol adds some latency
- ⚠️ **Mutter-Specific**: Requires Mutter's ScreenCast D-Bus API (GNOME only)

## DRM/KMS Direct Access (Our XFCE4 Approach)

### How It Works (Current Implementation):
1. **Virtual Connector Created**: Xorg modesetting driver creates virtual XR connector (XR-0) with off-screen framebuffer

2. **Direct Framebuffer Access**: Renderer directly maps the DRM framebuffer memory:
   ```c
   drmModeGetFB() -> drmModeMapDumb() -> mmap()
   ```

3. **Memory Copy (Current)**: Copy pixels from mapped memory to renderer's texture buffer
   - This adds latency and CPU overhead
   - **Can be optimized** - see Zero-Copy Optimization below

### Performance Characteristics (Current Implementation):
- ✅ **Low Latency**: Direct memory access, no protocol overhead
- ✅ **Full Control**: We control format, timing, and access patterns
- ✅ **No Dependencies**: Doesn't require Mutter or PipeWire APIs
- ⚠️ **CPU Copy Overhead**: Currently copying pixels from mapped memory to texture (can be optimized)
- ⚠️ **Manual Format Handling**: We need to handle format conversion ourselves
- ⚠️ **More Complex**: Need to manage DRM resources, buffer synchronization

### Zero-Copy Optimization (Future Enhancement)

**Yes, we can avoid the copy!** The best approach is **DMA-BUF import** via EGL:

1. **DMA-BUF Export**: The DRM framebuffer can be exported as a DMA-BUF file descriptor:
   ```c
   int dmabuf_fd;
   drmPrimeHandleToFD(drm_fd, fb_info->handle, DRM_CLOEXEC, &dmabuf_fd);
   ```

2. **EGL Image Import**: Import the DMA-BUF directly as an EGL image (zero-copy):
   ```c
   EGLImageKHR egl_image = eglCreateImageKHR(
	   egl_display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL,
	   attribs  // width, height, format, fd, offsets, strides, modifiers
   );
   ```

3. **OpenGL Texture from EGL Image**: Create texture directly from EGL image:
   ```c
   glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image);
   ```

**Benefits:**
- ✅ **Zero-copy**: GPU-to-GPU transfer, no CPU involvement
- ✅ **Hardware-accelerated**: Uses GPU memory directly
- ✅ **Lower latency**: No CPU copy = < 1 frame latency
- ✅ **Lower CPU usage**: No pixel copying overhead

**Requirements:**
- EGL extensions: `EGL_EXT_image_dma_buf_import`, `EGL_EXT_image_dma_buf_import_modifiers`
- GL extensions: `GL_OES_EGL_image` or `GL_EXT_EGL_image_storage`
- DRM framebuffer must support DMA-BUF export (most modern drivers do)

**This is exactly what Mutter uses** - it imports DMA-BUF directly as textures for zero-copy compositing.

### Alternative: Direct Texture Write (Not Recommended)

**Can the compositor write directly to the renderer's texture buffer?**

Technically possible but **not recommended** because:
- ❌ **Tight coupling**: Compositor must know about renderer's texture buffers
- ❌ **Synchronization complexity**: Requires complex coordination
- ❌ **Performance**: Still needs synchronization overhead
- ❌ **Architecture**: Violates separation of concerns

**Better approach**: Use DMA-BUF sharing (standard Linux graphics stack approach)

## Comparison

| Aspect | PipeWire via ScreenCast | DRM/KMS Direct |
|--------|------------------------|----------------|
| **Latency** | ~1-2 frames (protocol overhead) | < 1 frame (direct access) |
| **CPU Usage** | Lower (DMA-BUF zero-copy) | Higher (CPU copy, but can optimize) |
| **Complexity** | Lower (protocol handles details) | Higher (manual resource management) |
| **Availability** | GNOME/Mutter only | Works on any compositor |
| **Hardware Accel** | Yes (DMA-BUF) | Possible (but more complex) |
| **Best For** | When Mutter available | When creating our own virtual connector |

## Can We Use PipeWire in XFCE4?

### Option 1: Create Our Own ScreenCast-like Service
**Feasibility**: Possible but complex
- Would need to implement D-Bus service similar to Mutter's ScreenCast
- Would need to integrate with XFCE4 compositor
- Would need to create PipeWire streams from XR connector framebuffers
- **Complexity**: High
- **Benefit**: Could match GNOME's approach

### Option 2: Use PipeWire Lower-Level APIs Directly
**Feasibility**: Moderate
- XFCE4 has PipeWire available as a system service
- Could use `libpipewire` to create streams from our virtual connector
- Would still need to integrate framebuffer → PipeWire stream
- **Complexity**: Medium-High
- **Benefit**: Reuse PipeWire optimizations while staying independent

### Option 3: Stick with DRM/KMS Direct (Current Approach) ✅
**Feasibility**: Implemented
- We control the virtual connector completely
- Direct framebuffer access = lowest latency
- No external dependencies beyond libdrm (standard)
- Can optimize for our specific use case
- **Complexity**: Medium
- **Benefit**: Best performance for our architecture, no protocol overhead

## Recommendation: DRM/KMS Direct with DMA-BUF Optimization

For our use case (XFCE4 + custom virtual connector in Xorg driver), **DRM/KMS direct access is the better choice** because:

1. **We Own the Virtual Connector**: Since we're creating it in the Xorg driver, we have full control and can optimize access patterns

2. **Lowest Latency Possible**: With DMA-BUF import, we can achieve zero-copy GPU-to-GPU transfer = < 1 frame latency

3. **No External APIs Needed**: Don't need Mutter's D-Bus API or complex PipeWire integration

4. **Optimization Path Clear**: Start with CPU copy (simple), then optimize to DMA-BUF import (zero-copy)

5. **Simplicity**: One less moving part = easier to debug and maintain

## Implementation Strategy

### Phase 1: Initial Implementation (CPU Copy)
- Map DRM framebuffer with `mmap()`
- Copy pixels to OpenGL texture buffer
- **Acceptable for initial testing** - capture at 60fps, render at 90fps
- CPU overhead is manageable on modern systems

### Phase 2: Zero-Copy Optimization (DMA-BUF Import)
- Export DRM framebuffer as DMA-BUF file descriptor
- Import DMA-BUF as EGL image
- Create OpenGL texture directly from EGL image
- **Result**: Zero-copy, hardware-accelerated, minimal latency

This is the **standard approach used by Mutter and other modern compositors** - DMA-BUF sharing for zero-copy GPU-to-GPU transfers.

