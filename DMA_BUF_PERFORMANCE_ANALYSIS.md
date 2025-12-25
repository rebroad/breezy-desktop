# DMA-BUF Export Performance Analysis

## Current Implementation Issue

The current renderer implementation exports the DMA-BUF file descriptor **on every frame**:

```c
// In capture_thread_func - called every frame!
while (!thread->stop_requested) {
    int export_result = export_drm_framebuffer_to_dmabuf(thread, &dmabuf_fd, ...);
    // ... use dmabuf_fd ...
}
```

This means **every frame**:
1. `drmModeGetFB()` - query framebuffer info
2. `drmPrimeHandleToFD()` - export BO handle to DMA-BUF FD (kernel syscall)

This is **inefficient** because:
- DMA-BUF FDs can be reused - the same framebuffer handle can use the same FD
- Exporting creates a new FD each time, which must be closed later (file descriptor churn)
- Unnecessary kernel syscalls on every frame (~60-120 per second)

## Performance Comparison

### Current Approach (Per-Frame Export)

**Every frame:**
- `drmModeGetFB()` syscall
- `drmPrimeHandleToFD()` syscall (creates new FD)
- Old FD must be closed (when render thread consumes it)

**Cost:** ~2-3 syscalls per frame = **120-360 syscalls/second** at 60fps

### Optimized Approach (Export Once, Reuse FD)

**Initialization (once or on framebuffer change):**
- `drmModeGetFB()` syscall
- `drmPrimeHandleToFD()` syscall (creates FD)
- Store FD for reuse

**Per frame:**
- Reuse existing FD (no syscalls)

**Cost:** ~2 syscalls on init/mode change only = **~0 syscalls/second** during normal operation

### Socket Approach (Per-Frame Query - Bad)

**Every frame:**
- Socket connect/query
- Xorg exports FD via `drmPrimeHandleToFD()`
- Receive FD via `SCM_RIGHTS`
- Socket communication overhead

**Cost:** Socket IPC + export syscalls per frame = **worse than current approach**

## Conclusion: Socket is NOT an Optimization

The document `KMS_CONNECTOR_AND_FB_ID_OPTIONS.md` suggests a socket approach as an optimization, but this is **misleading**:

1. **Socket would be worse** - it doesn't solve the per-frame export problem, it just moves it to Xorg
2. **The real optimization** is to export the DMA-BUF FD **once** and reuse it until the framebuffer changes
3. **Both approaches** (property + export, or socket + export) can be optimized the same way - export once, reuse

## Recommended Fix

### Option A: Export Once, Reuse FD (Recommended)

**Capture thread:**
- Export DMA-BUF FD **once** during initialization
- Reuse the same FD for all frames
- Only re-export when framebuffer changes (detected via error from `drmModeGetFB()`)

**Implementation:**
```c
// In capture thread - export once
int init_drm_capture(CaptureThread *thread) {
    // ... existing init code ...
    
    // Export DMA-BUF FD once (not per frame)
    int dmabuf_fd = -1;
    if (export_drm_framebuffer_to_dmabuf(thread, &dmabuf_fd, ...) < 0) {
        return -1;
    }
    thread->dmabuf_fd = dmabuf_fd;  // Store for reuse
    return 0;
}

// In capture loop - reuse FD, only re-export on error
while (!thread->stop_requested) {
    // Verify FB still exists (lightweight check)
    drmModeFBPtr fb_check = drmModeGetFB(thread->drm_fd, thread->fb_id);
    if (!fb_check) {
        // FB changed - re-export
        close(thread->dmabuf_fd);
        export_drm_framebuffer_to_dmabuf(thread, &thread->dmabuf_fd, ...);
    } else {
        drmModeFreeFB(fb_check);
    }
    
    // Reuse existing dmabuf_fd - no export needed!
    pass_fd_to_render_thread(thread->dmabuf_fd);
}
```

**Benefits:**
- ✅ Eliminates per-frame export syscalls
- ✅ Reduces FD churn
- ✅ Still detects framebuffer changes (via `drmModeGetFB()` check)
- ✅ Simple change to existing code

### Option B: Export on Demand (Current + Better)

**Alternative approach:**
- Keep current architecture (FD passed per frame via shared state)
- But cache the FD in capture thread and only re-export when needed

**Implementation:**
```c
// Cache the exported FD
static int cached_dmabuf_fd = -1;
static uint32_t cached_fb_id = 0;

while (!thread->stop_requested) {
    // Check if we need to re-export
    if (cached_dmabuf_fd < 0 || cached_fb_id != thread->fb_id) {
        if (cached_dmabuf_fd >= 0) {
            close(cached_dmabuf_fd);
        }
        export_drm_framebuffer_to_dmabuf(thread, &cached_dmabuf_fd, ...);
        cached_fb_id = thread->fb_id;
    }
    
    // Reuse cached FD
    pass_fd_to_render_thread(cached_dmabuf_fd);
}
```

## Performance Impact

**Current (per-frame export):**
- 60fps × 2 syscalls = 120 syscalls/second
- Each export creates new FD (must be tracked/closed)
- Higher CPU overhead

**Optimized (export once, reuse):**
- 2 syscalls on init/mode change only
- Same FD reused for all frames
- ~99% reduction in syscall overhead

## Recommendation

**Fix the current implementation** to export DMA-BUF FD once and reuse it, rather than adding a socket mechanism. The socket approach would not improve performance and would add complexity.

The real performance gain comes from **not exporting every frame**, regardless of how you get the initial FD (property query or socket).

