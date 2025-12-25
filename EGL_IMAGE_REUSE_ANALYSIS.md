# EGL Image Reuse Analysis

## Current Implementation (Creates New EGL Image Every Frame)

### Current Flow

**Capture Thread (every frame):**
1. Duplicate cached DMA-BUF FD: `dup(cached_dmabuf_fd)`
2. Pass duplicate FD + FB ID to render thread via shared state

**Render Thread (every frame):**
1. Receive DMA-BUF FD + FB ID from capture thread
2. Create new EGL image: `eglCreateImageKHR(dmabuf_fd)`
3. Destroy old EGL image: `eglDestroyImageKHR(old_egl_image)` ← closes FD
4. Bind new EGL image to texture: `glEGLImageTargetTexture2DOES(texture, egl_image)`
5. Render frame

**Code Complexity:**
- Capture thread: Simple - just duplicate and pass
- Render thread: Always creates/destroys EGL image
- FD management: Each frame needs new FD (via dup)

---

## Alternative: Reuse EGL Image (Only Create When FB Changes)

### Proposed Flow

**Capture Thread (every frame):**
1. Check if framebuffer ID changed
2. If changed: duplicate cached DMA-BUF FD and pass to render thread
3. If unchanged: signal render thread that frame is ready (no FD needed)

**Render Thread (every frame):**
1. Check if new FB ID received from capture thread
2. If FB ID changed:
   - Destroy old EGL image
   - Create new EGL image from new FD
   - Bind to texture
3. If FB ID unchanged:
   - Reuse existing EGL image/texture (no EGL calls)
4. Render frame

---

## Code Comparison

### Current Implementation Code

**Capture thread (`breezy_x11_renderer.c`):**
```c
// Every frame - always duplicate
int dmabuf_fd_dup = dup(thread->cached_dmabuf_fd);
render_thread->current_dmabuf_fd = dmabuf_fd_dup;
render_thread->current_fb_id = thread->fb_id;
// ... pass to render thread ...
```

**Render thread (`breezy_x11_renderer.c` + `opengl_context.c`):**
```c
// Every frame - always create/destroy EGL image
if (dmabuf_fd >= 0) {
    GLuint texture = import_dmabuf_as_texture(thread, dmabuf_fd, ...);
    // import_dmabuf_as_texture always creates new EGL image
    // and destroys old one
}
```

**Lines of code:** ~20-30 lines for FD duplication + EGL image creation/destruction logic

---

### Reuse Implementation Code

**Capture thread (`breezy_x11_renderer.c`):**
```c
// Track last FB ID passed to render thread
static uint32_t last_passed_fb_id = 0;

while (!thread->stop_requested) {
    // ... framebuffer validation ...
    
    pthread_mutex_lock(&render_thread->dmabuf_mutex);
    
    // Only pass FD if framebuffer changed
    if (thread->fb_id != render_thread->current_fb_id) {
        // FB changed - need to update EGL image
        if (render_thread->current_dmabuf_fd >= 0) {
            close(render_thread->current_dmabuf_fd);  // Close old FD
        }
        
        int dmabuf_fd_dup = dup(thread->cached_dmabuf_fd);
        render_thread->current_dmabuf_fd = dmabuf_fd_dup;
        render_thread->current_fb_id = thread->fb_id;
        render_thread->current_format = thread->cached_format;
        render_thread->current_stride = thread->cached_stride;
        render_thread->current_modifier = thread->cached_modifier;
        render_thread->fb_changed = true;  // Signal render thread
    } else {
        // FB unchanged - just signal new frame available
        render_thread->fb_changed = false;
    }
    
    pthread_mutex_unlock(&render_thread->dmabuf_mutex);
    
    // Signal frame available (regardless of FB change)
    write_frame(...);
}
```

**Render thread (`breezy_x11_renderer.c`):**
```c
// In render_frame():
pthread_mutex_lock(&thread->dmabuf_mutex);

bool fb_changed = thread->fb_changed;
int dmabuf_fd = -1;
if (fb_changed && thread->current_dmabuf_fd >= 0) {
    dmabuf_fd = thread->current_dmabuf_fd;
    thread->current_dmabuf_fd = -1;  // Take ownership
    thread->fb_changed = false;
}
uint32_t fb_id = thread->current_fb_id;
// ... format, stride, modifier ...

pthread_mutex_unlock(&thread->dmabuf_mutex);

// Only create new EGL image if framebuffer changed
if (fb_changed && dmabuf_fd >= 0) {
    GLuint texture = import_dmabuf_as_texture(thread, dmabuf_fd, ...);
    // This creates new EGL image and destroys old one
} else if (!fb_changed) {
    // Reuse existing texture/EGL image - no EGL calls needed!
    // Just proceed to rendering
}

// Render frame (texture already bound from previous frame or just set)
```

**Render thread (`opengl_context.c` - modified `import_dmabuf_as_texture`):**
```c
GLuint import_dmabuf_as_texture(RenderThread *thread, int dmabuf_fd, ...) {
    // ... EGL setup code ...
    
    // Cleanup old EGL image if it exists (FB changed)
    if (thread->frame_egl_image != EGL_NO_IMAGE_KHR) {
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, EGL_NO_IMAGE_KHR);
        eglDestroyImageKHR(egl_display, thread->frame_egl_image);
    }
    
    // Create new EGL image (same as current)
    EGLImageKHR egl_image = eglCreateImageKHR(...);
    // ... bind to texture ...
    
    thread->frame_egl_image = egl_image;
    return thread->frame_texture;
}
```

**Lines of code:** ~40-50 lines (more complex conditionals + state tracking)

---

## Code Complexity Comparison

### Current Implementation
- **Lines of code:** ~25 lines
- **Complexity:** Low - always does the same thing every frame
- **State tracking:** Minimal (just pass FD + FB ID)
- **Conditionals:** None (always create EGL image)

### Reuse Implementation
- **Lines of code:** ~45 lines
- **Complexity:** Medium - conditional logic based on FB ID change
- **State tracking:** More (track `fb_changed` flag, compare FB IDs)
- **Conditionals:** Multiple (check if FB changed, decide whether to create EGL image)

**Result:** **Reuse implementation requires MORE code** (~80% more lines)

---

## Performance Comparison

### Current Implementation (Every Frame - Creates/Destroys EGL Image)

**Per frame:**
- `dup()` syscall: ~0.1μs (very fast)
- `eglCreateImageKHR()`: ~10-50μs (depends on driver)
- `eglDestroyImageKHR()`: ~5-20μs (depends on driver)
- Total: ~15-70μs per frame

**At 60fps:** 
- ~0.9-4.2ms per second of CPU time
- ~54-252ms per minute
- This overhead happens on **EVERY SINGLE FRAME** (60-120 times per second)

### Reuse Implementation (Only Create/Destroy When FB Changes)

**First frame (FB unchanged):**
- `dup()` syscall: ~0.1μs
- `eglCreateImageKHR()`: ~10-50μs
- `eglDestroyImageKHR()`: 0μs (no old image to destroy)
- Total: ~10-50μs

**Per frame (FB unchanged - 99.99% of frames):**
- No `dup()` syscall (FD already passed)
- No EGL calls (reuse existing EGL image)
- Total: **~0μs per frame**

**Per frame (FB changed - rare, maybe once per session):**
- `dup()` syscall: ~0.1μs
- `eglCreateImageKHR()`: ~10-50μs
- `eglDestroyImageKHR()`: ~5-20μs
- Total: ~15-70μs (same as current, but happens only when FB changes)

**At 60fps over 1 minute:**
- 3600 frames total
- ~3599 frames (FB unchanged): 0μs each = 0μs
- 1 frame (FB changed): ~15-70μs
- **Total: ~15-70μs per minute** (vs ~54-252ms for current implementation)
- **Savings: ~54-252ms per minute** (99.97% reduction in EGL image overhead!)

---

## Trade-offs

### Advantages of Reuse

1. **Performance:** Eliminates EGL image create/destroy overhead on every frame
2. **Efficiency:** No `dup()` syscall when FB unchanged
3. **CPU usage:** Lower CPU usage (especially at high frame rates)

### Disadvantages of Reuse

1. **Code complexity:** More conditionals and state tracking
2. **Bug potential:** More complex code = more potential for bugs
3. **Maintenance:** Harder to understand and maintain
4. **Minimal gain:** The performance improvement is small (15-70μs per frame is negligible)
5. **EGL image lifecycle:** Need to carefully manage when to create/destroy

### When FB Changes (Rare Events)

**FB changes happen when:**
- User changes resolution/refresh rate (rare - once per session maybe)
- Mode switch (rare)
- Virtual output deleted/recreated (rare)

**In practice:** FB changes are extremely rare - maybe once per session, or never. This means 99.99% of frames can reuse the EGL image.

**Impact of reuse optimization:**
- **Saves ~15-70μs on EVERY frame** where FB is unchanged
- At 60fps, that's ~0.9-4.2ms saved per second
- Over a 1-minute session: ~54-252ms saved
- Over a 10-minute session: ~0.5-2.5 seconds saved
- This is a **significant performance improvement** for a frequently executed operation

---

## Edge Cases to Handle (Reuse Implementation)

1. **First frame:** No existing EGL image, must create one
2. **FB change:** Must destroy old EGL image before creating new one
3. **Concurrent access:** Mutex must protect `fb_changed` flag
4. **Texture reuse:** Need to ensure texture is still valid if EGL image reused
5. **Error handling:** What if EGL image creation fails? Need fallback

**Current implementation handles these implicitly** because it always creates fresh.

---

## Recommendation

### Performance Gain Analysis

**Current implementation overhead per frame:** ~15-70μs
- This is ~0.09-0.42% of a 16.67ms frame (at 60fps)
- Seems small per frame, but accumulates significantly over time
- At 60fps: ~0.9-4.2ms per second, ~54-252ms per minute

**With reuse optimization:**
- Overhead drops to ~0μs for 99.99% of frames
- Saves ~54-252ms per minute of runtime
- This is a **meaningful performance improvement** for a hot path

### Recommendation: It Depends on Priorities

**Arguments for Current Implementation (simpler):**
1. **Simplicity:** Much simpler code (~25 lines vs ~45 lines)
2. **Maintainability:** Easier to understand and debug
3. **Small per-frame overhead:** 15-70μs is only ~0.1-0.4% of frame time
4. **The expensive operation is already optimized:** `drmPrimeHandleToFD()` was the real bottleneck, and we've eliminated it

**Arguments for Reuse Implementation (faster):**
1. **Significant cumulative savings:** ~54-252ms per minute adds up
2. **Eliminates unnecessary work:** Why create/destroy EGL images every frame if not needed?
3. **Better CPU efficiency:** Lower CPU usage over time
4. **Cleaner design:** Only update resources when they actually change

**The real optimization was eliminating `drmPrimeHandleToFD()` every frame**, which we've already done. However, the EGL image create/destroy overhead, while small per frame, does accumulate over time and could be worth optimizing if code complexity is acceptable.

### When to Consider Each Approach

**Choose Current Implementation (simpler) if:**
- Code simplicity and maintainability are top priorities
- The 0.1-0.4% per-frame overhead is acceptable
- Team prefers simpler code over micro-optimizations
- You want to minimize bug potential

**Choose Reuse Implementation (faster) if:**
- You want to minimize CPU usage over long sessions
- Code complexity is acceptable for your team
- You're optimizing for efficiency and best performance
- You value eliminating unnecessary work even if the savings seem small per frame
- High frame rates (120fps+) where overhead accumulates faster

---

## Conclusion

**Reusing EGL images would require MORE code** (~80% more) and add complexity, but provides a **meaningful performance benefit**:

- **Current:** Creates/destroys EGL image every frame (~15-70μs per frame, ~54-252ms per minute)
- **Reuse:** Only creates/destroys when FB changes (~15-70μs only when FB changes, ~0μs for 99.99% of frames)

**Performance savings:** ~54-252ms per minute, ~0.5-2.5 seconds per 10-minute session

**Trade-off:** More complex code (~45 lines vs ~25 lines) for better efficiency.

The significant optimization (eliminating `drmPrimeHandleToFD()` every frame) has already been implemented. Whether to also optimize EGL image creation depends on your priorities: **simplicity vs. efficiency**.

Both approaches are valid - it's a classic simplicity vs. performance trade-off.

