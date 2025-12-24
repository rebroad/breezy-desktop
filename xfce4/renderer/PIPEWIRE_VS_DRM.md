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

### How It Works:
1. **Virtual Connector Created**: Xorg modesetting driver creates virtual XR connector (XR-0) with off-screen framebuffer

2. **Direct Framebuffer Access**: Renderer directly maps the DRM framebuffer memory:
   ```c
   drmModeGetFB() -> drmModeMapDumb() -> mmap()
   ```

3. **Direct Memory Read**: Copy pixels directly from mapped memory to renderer's texture buffer

### Performance Characteristics:
- ✅ **Lowest Latency**: Direct memory access, no protocol overhead
- ✅ **Full Control**: We control format, timing, and access patterns
- ✅ **No Dependencies**: Doesn't require Mutter or PipeWire APIs
- ⚠️ **Manual Format Handling**: We need to handle format conversion ourselves
- ⚠️ **More Complex**: Need to manage DRM resources, buffer synchronization

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

## Recommendation: DRM/KMS Direct

For our use case (XFCE4 + custom virtual connector in Xorg driver), **DRM/KMS direct access is the better choice** because:

1. **We Own the Virtual Connector**: Since we're creating it in the Xorg driver, we have full control and can optimize access patterns

2. **Lower Latency**: Direct memory access means < 1 frame latency vs PipeWire's ~1-2 frames

3. **No External APIs Needed**: Don't need Mutter's D-Bus API or complex PipeWire integration

4. **Can Still Optimize**: We can implement DMA-BUF-like optimizations later if needed, but for now the CPU copy is acceptable (capture thread runs at 60fps, render thread at 90fps - plenty of headroom)

5. **Simplicity**: One less moving part = easier to debug and maintain

## Future Optimization: Hybrid Approach

If we need better performance later, we could:
1. Keep DRM/KMS for capture (lowest latency)
2. Add DMA-BUF support for zero-copy GPU-to-GPU transfer
3. Use EGL image extensions to avoid CPU copy entirely

But for initial implementation, DRM/KMS direct is the right choice.

