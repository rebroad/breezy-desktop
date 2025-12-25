# Framebuffer Changes (Mode Switches) - Handling and Detection

## When Do Framebuffer Changes Occur?

**Framebuffer changes happen when:**

1. **User changes XR-0 resolution via Display Settings**
   - Example: User switches XR-0 from 1920x1080 to 2560x1440 via `xrandr` or GUI
   - Xorg calls `drmmode_xr_virtual_crtc_set_mode_major()` in the modesetting driver
   - Driver destroys old framebuffer and creates new one with different size
   - **New framebuffer ID** is generated (old one is invalidated)

2. **User changes XR-0 refresh rate**
   - Example: User switches from 60Hz to 120Hz
   - Similar to resolution change - framebuffer is recreated

3. **Virtual output is deleted and recreated**
   - If `breezy-desktop` deletes and recreates XR-0
   - New framebuffer is allocated (new ID)

## Current Implementation Status

### Xorg Driver Side (`drmmode_xr_virtual.c`)

**✅ Already Handles Mode Changes:**

```c
static void
drmmode_xr_virtual_crtc_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode,
                                       Rotation rotation, int x, int y)
{
    // Detects mode size changes
    if (old_width != new_width || old_height != new_height) {
        // Destroys old framebuffer
        drmmode_xr_destroy_offscreen_framebuffer(...);
        
        // Creates new framebuffer at new resolution
        drmmode_xr_create_offscreen_framebuffer(..., new_width, new_height);
        
        // Updates FRAMEBUFFER_ID property with new ID
        drmmode_xr_virtual_ensure_fb_id_property(vout, new_fb_id);
    }
}
```

**What happens:**
- Old framebuffer is destroyed (`drmModeRmFB` - framebuffer ID becomes invalid)
- New framebuffer is created (new ID assigned)
- `FRAMEBUFFER_ID` RandR property is **updated** with the new ID
- Virtual output dimensions (`vout->width`, `vout->height`) are updated

### Renderer Side (`breezy_x11_renderer.c` / `drm_capture.c`)

**⚠️ Currently Does NOT Handle Mode Changes:**

The renderer:
1. **Queries framebuffer ID once** during `init_drm_capture()` (line 194)
2. **Stores the ID** in `thread->fb_id` (line 200)
3. **Reuses the same ID** for all subsequent frame captures (line 231)

**Problem:**
- If XR-0 resolution changes, the old framebuffer ID becomes invalid
- `drmModeGetFB(drm_fd, old_fb_id)` will fail
- Renderer will start getting errors, but won't automatically detect or recover

## Does the Renderer Need to Handle This?

**Yes, but it's not urgent for initial testing.**

### Why It Matters:

1. **User Experience:** Users might want to change virtual display resolution
2. **Error Recovery:** Without detection, renderer will silently fail or crash
3. **Proper Cleanup:** Old DMA-BUF FDs should be closed when framebuffer changes

### How Often Does It Happen?

- **Rarely during normal use** - resolution/refresh changes are infrequent
- **Common during initial setup** - user might try different resolutions
- **Testing scenarios** - developers might test various resolutions

### Priority:

- **Medium Priority** - not blocking for initial implementation
- **Can be added as enhancement** after basic functionality works
- **Important for production** - users will change resolutions

## Implementation Options

### Option 1: Poll FRAMEBUFFER_ID Property (Recommended)

**Mechanism:**
- Periodically (every N frames or every second) re-query the `FRAMEBUFFER_ID` property
- If ID changed, re-initialize DRM capture with new framebuffer

**Implementation:**
```c
// In capture_thread_func, every N frames:
static uint32_t last_queried_fb_id = 0;
static int frame_count = 0;

frame_count++;
if (frame_count % 60 == 0) {  // Check every 60 frames (~1 second at 60fps)
    uint32_t current_fb_id = query_framebuffer_id_from_randr("XR-0");
    if (current_fb_id != 0 && current_fb_id != thread->fb_id) {
        log_info("[DRM] Framebuffer changed: %u -> %u, reinitializing\n",
                 thread->fb_id, current_fb_id);
        
        // Cleanup old framebuffer
        cleanup_drm_capture(thread);
        
        // Re-initialize with new framebuffer
        if (init_drm_capture(thread) < 0) {
            log_error("[DRM] Failed to reinitialize after framebuffer change\n");
            // Handle error - maybe exit or retry?
        }
        
        last_queried_fb_id = current_fb_id;
    }
}
```

**Pros:**
- ✅ Simple to implement
- ✅ No X11 event handling needed
- ✅ Works reliably
- ✅ Low overhead (query every second, not every frame)

**Cons:**
- ❌ Slight delay in detecting changes (up to 1 second)
- ❌ Periodic X11 round-trip (but infrequent)

**Recommendation: ⭐⭐⭐⭐⭐ Best option**

### Option 2: XRandR Property Change Notifications

**Mechanism:**
- Register for XRandR property change events on XR-0 output
- When `FRAMEBUFFER_ID` property changes, re-initialize DRM capture

**Implementation:**
```c
// In init_drm_capture or capture thread:
Display *dpy = XOpenDisplay(NULL);
Atom fb_id_atom = XInternAtom(dpy, "FRAMEBUFFER_ID", False);
XRRSelectInput(dpy, screen_res->outputs[output_idx],
               RROutputPropertyNotifyMask);

// In event loop (separate thread or poll):
XEvent ev;
if (XCheckTypedEvent(dpy, RREventBase + RRNotify, &ev)) {
    XRRScreenChangeNotifyEvent *sce = (XRRScreenChangeNotifyEvent *)&ev;
    if (sce->subtype == RRNotify_OutputProperty) {
        XRRPropertyNotifyEvent *pne = (XRRPropertyNotifyEvent *)sce;
        if (pne->atom == fb_id_atom) {
            // Property changed, re-initialize
        }
    }
}
```

**Pros:**
- ✅ Immediate detection (no polling delay)
- ✅ Event-driven (efficient)

**Cons:**
- ❌ More complex (need X11 event loop)
- ❌ Threading complexity (need to handle X11 events in capture thread or separate thread)
- ❌ X11 event handling can be tricky

**Recommendation: ⭐⭐⭐ Good option, but more complex than needed**

### Option 3: Error-Based Detection (Recommended - Implemented)

**Mechanism:**
- When `drmModeGetFB()` fails (returns NULL), the framebuffer ID has been invalidated
- This happens immediately when Xorg calls `drmModeRmFB()` to destroy the old framebuffer
- Re-query `FRAMEBUFFER_ID` property and re-initialize DRM capture

**Why this works:**
- ✅ **Immediate detection** - `drmModeGetFB()` fails as soon as framebuffer is destroyed
- ✅ **No polling overhead** - detection happens naturally when we try to capture
- ✅ **Simple implementation** - just check if `drmModeGetFB()` fails before export
- ✅ **No race conditions** - if FB is destroyed, it's invalid immediately

**Implementation:**
```c
// In export_drm_framebuffer_to_dmabuf:
// Verify framebuffer still exists before exporting
drmModeFBPtr fb_check = drmModeGetFB(thread->drm_fd, thread->fb_id);
if (!fb_check) {
    // Framebuffer was destroyed - likely due to resolution change
    log_warn("[DRM] Framebuffer ID %u no longer valid, likely due to mode change\n", thread->fb_id);
    return -2;  // FRAMEBUFFER_CHANGED
}
drmModeFreeFB(fb_check);

// In capture thread:
if (export_drm_framebuffer_to_dmabuf(...) == -2) {
    // Framebuffer changed, re-initialize
    cleanup_drm_capture(thread);
    init_drm_capture(thread);  // Will query new FB ID from RandR property
}
```

**Pros:**
- ✅ Simple to implement
- ✅ No polling overhead
- ✅ Immediate detection (no delay)
- ✅ Naturally integrated into capture flow
- ✅ No race conditions

**Cons:**
- ⚠️ Detection is **reactive** (after FB is destroyed)
- ⚠️ One frame capture might fail before recovery

**Note:** The one-frame delay is acceptable because:
- Mode changes are rare (user-initiated)
- The next frame capture will immediately detect and recover
- Xorg updates the `FRAMEBUFFER_ID` property atomically with framebuffer creation
- No polling or event handling complexity needed

**Recommendation: ⭐⭐⭐⭐⭐ Best option - simple, efficient, reliable**

## Summary

**Framebuffer changes happen when:**
- User changes XR-0 resolution or refresh rate via Display Settings
- Virtual output is deleted/recreated

**Does framebuffer ID have to change?**
- **Yes** - when resolution changes, Xorg:
  1. Calls `drmModeRmFB()` to destroy old framebuffer (old ID becomes invalid immediately)
  2. Creates new framebuffer with `drmModeAddFB()` (new ID assigned)
  3. Updates `FRAMEBUFFER_ID` RandR property with new ID

**Crash risk if renderer uses old FB ID?**
- **No crash risk** - `drmModeGetFB()` will return NULL/fail safely
- The error is detected before DMA-BUF export is attempted
- Error-based detection works perfectly - no polling needed

**Current status:**
- ✅ Xorg driver handles mode changes correctly (destroys old, creates new, updates property)
- ✅ Renderer handles mode changes via error-based detection (implemented)

**Implementation:**
- **Error-based detection only** - check if `drmModeGetFB()` fails, re-initialize if so
- **Priority:** ✅ Implemented - ready for testing

