# DRM Device Security and Permissions

## Device Path and Ownership

**DRM devices are located at:**
- `/dev/dri/card0`, `/dev/dri/card1`, etc. - Main DRM devices
- `/dev/dri/renderD128`, `/dev/dri/renderD129`, etc. - Render nodes (preferred for non-root access)

**Typical permissions and ownership:**
```bash
$ ls -la /dev/dri/
crw-rw---- 1 root video 226, 0 Jan 1 12:00 card0
crw-rw---- 1 root render 226, 128 Jan 1 12:00 renderD128
```

- **Owner:** `root`
- **Group:** `video` (for card nodes) or `render` (for render nodes)
- **Permissions:** `0660` (rw-rw----)
- **Major number:** `226`
- **Minor numbers:** `0, 1, 2...` (card nodes) or `128, 129, 130...` (render nodes)

## Security Considerations

### 1. **Direct Hardware Access**

**Risk:**
- `/dev/dri/cardN` provides direct access to graphics hardware
- Can read/modify display framebuffers
- Can potentially access GPU memory
- Can program display hardware (modesetting, etc.)

**Mitigation:**
- Access controlled via group membership (`video` or `render` groups)
- User must be member of appropriate group
- Standard Linux permission model (group-based access control)

### 2. **Framebuffer Access**

**What the renderer does:**
- Opens DRM device (`/dev/dri/cardN`)
- Reads framebuffer information via `drmModeGetFB()`
- Exports framebuffer to DMA-BUF for zero-copy capture
- **Reads display content** from virtual XR outputs

**Security implications:**
- ✅ **Read-only access is generally safe** - reading framebuffer doesn't compromise system security
- ⚠️ **Can see display content** - including other users' content if multi-user system
- ✅ **Virtual output content** - reading from XR-0 is safe (it's the user's own virtual display)

### 3. **Multi-User Systems**

**Risk:**
- On multi-user systems, any user in `video`/`render` group can access DRM devices
- Could potentially read other users' display content
- Could access other applications' framebuffers

**Mitigation:**
- Modern Linux systems use `render` group (render-only nodes) instead of `video` group
- Render nodes (`/dev/dri/renderD128`) allow rendering without modesetting privileges
- For reading framebuffers, `video` or `render` group membership is typically sufficient
- Users in these groups are trusted users (usually single-user desktop systems)

### 4. **Modesetting Capabilities**

**What our renderer needs:**
- **Read-only access** to framebuffers (for capture)
- **No modesetting** required (we're reading, not configuring displays)
- **No CRTC programming** needed (virtual outputs are managed by Xorg)

**Security benefit:**
- Renderer only needs **read access**, not full control
- Render nodes (`renderD128`) provide this level of access
- More restrictive than full card node access

## Recommended Approach

### Option 1: Use Render Nodes (Preferred)

**Device:** `/dev/dri/renderD128` (or renderD129, etc.)

**Advantages:**
- ✅ More restrictive access (render-only, no modesetting)
- ✅ Better security model
- ✅ Modern standard for GPU rendering applications
- ✅ Sufficient for reading framebuffers and DMA-BUF export

**Implementation:**
```c
// Find render node instead of card node
#define DRM_DEVICE_PATH "/dev/dri"
#define DRM_DEVICE_PREFIX "renderD"  // Instead of "card"

// Open render node
int drm_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
```

**Permissions needed:**
- User must be in `render` group
- Standard for desktop users (most distributions add user to `render` group automatically)

### Option 1a: Try Both Render Nodes and Card Nodes (Recommended Implementation)

**Hybrid approach:**
- Try render nodes first (more secure)
- Fall back to card nodes if render nodes don't work
- Works regardless of which group the user is in

**Implementation:**
```c
static int find_drm_device_for_framebuffer(uint32_t fb_id, char *device_path, size_t path_size) {
    // Try render nodes first (renderD128, renderD129, ...)
    if (try_device_prefix(fb_id, "renderD", device_path, path_size) == 0) {
        return 0;
    }
    
    // Fall back to card nodes (card0, card1, ...)
    if (try_device_prefix(fb_id, "card", device_path, path_size) == 0) {
        log_info("[DRM] Using card node (render node not available)\n");
        return 0;
    }
    
    return -1;  // Not found
}
```

**Advantages:**
- ✅ Works with either `video` or `render` group membership
- ✅ Prefers more secure render nodes when available
- ✅ Graceful fallback to card nodes
- ✅ Better user experience (no need to check which group they're in)

**Recommendation: ⭐⭐⭐⭐⭐ Best option for maximum compatibility**

### Option 2: Use Card Nodes (Current)

**Device:** `/dev/dri/card0` (or card1, etc.)

**Advantages:**
- ✅ Works for both reading and modesetting
- ✅ More compatible (older systems)

**Disadvantages:**
- ⚠️ More permissive (allows modesetting if user is in `video` group)
- ⚠️ Less secure (broader access to hardware)

**Permissions needed:**
- User must be in `video` group
- More restrictive distribution policy

## Current Implementation

**Our renderer currently:**
1. Queries framebuffer ID via XRandR property on XR-0 output
2. Iterates through `/dev/dri/card*` devices to find which one has the framebuffer
3. Opens the matching DRM device
4. Uses `drmModeGetFB()` and `drmPrimeHandleToFD()` for zero-copy capture

**How Device Selection Works:**

The renderer doesn't know in advance which DRM device (card0, card1, renderD128, etc.) contains the framebuffer. It uses a **trial-and-error approach**:

```c
// In find_drm_device_for_framebuffer():
// 1. Iterate through all devices in /dev/dri/
for each entry in /dev/dri/ {
    if entry name starts with "card" {
        // 2. Try to open the device
        drm_fd = open("/dev/dri/cardN", O_RDWR | O_CLOEXEC);
        
        // 3. Check if this device has our framebuffer ID
        fb_info = drmModeGetFB(drm_fd, fb_id);
        if (fb_info) {
            // Found it! This device owns the framebuffer
            return device_path;
        }
        close(drm_fd);
    }
}
```

**Why this works:**
- Each DRM device (card0, card1, etc.) can have multiple framebuffers
- A framebuffer ID is unique **within a DRM device**, but not globally
- By trying each device, we find which one owns our specific framebuffer ID
- Once found, we use that device for all subsequent operations

**Security assessment:**
- ✅ **Read-only framebuffer access** - no modesetting or display control
- ✅ **Virtual output content** - reading user's own virtual display (XR-0)
- ✅ **Tries render nodes first** - falls back to card nodes for maximum compatibility
- ✅ **Standard permissions** - requires user to be in `video` or `render` group (normal for desktop users)

## Recommendations

### For Current Implementation

1. **Enhance device selection** - try render nodes first, fall back to card nodes
   - Works with either `video` or `render` group membership
   - Prefers more secure render nodes when available
   - Better user experience (no need to check which group they're in)
2. **Document requirement** - user must be in `video` or `render` group
3. **Add helpful error messages** - if device access fails, suggest checking group membership

### For Better Security (Future Enhancement)

1. **Use render nodes** (`/dev/dri/renderD128`) instead of card nodes
   - More restrictive access model
   - Standard for rendering applications
   - Still supports framebuffer reading and DMA-BUF export

2. **Add permission check in renderer:**
   ```c
   // Check if user has access to DRM device
   int test_fd = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
   if (test_fd < 0) {
       log_error("No access to DRM device - user must be in 'render' group\n");
       return -1;
   }
   close(test_fd);
   ```

3. **Document security implications:**
   - Read access to display content
   - Group membership requirement
   - Multi-user system considerations

## Comparison: Card Nodes vs Render Nodes

| Aspect                | Card Nodes<br>`/dev/dri/cardN` | Render Nodes<br>`/dev/dri/renderD128` |
|-----------------------|--------------------------------|---------------------------------------|
| **Group**             | `video`                        | `render`                              |
| **Modesetting**       | ✅ Yes (if in group)           | ❌ No                                 |
| **Framebuffer Read**  | ✅ Yes                         | ✅ Yes                                |
| **DMA-BUF Export**    | ✅ Yes                         | ✅ Yes                                |
| **Security**          | ⚠️ More permissive             | ✅ More restrictive                   |
| **Use Case**          | Display control + rendering    | Rendering only                        |
| **Our Needs**         | ✅ Sufficient                  | ✅ Better fit (we only read)          |

## Conclusion

**Current security level: Acceptable for desktop use**

- Requires group membership (`video` or `render`)
- Standard permission model
- Read-only access to user's own virtual display
- No significant security risk for single-user desktop systems

**Implemented enhancement: Hybrid approach (render nodes + card nodes)**

- ✅ **Implemented:** Tries render nodes first (more secure), falls back to card nodes
- ✅ Works with either `video` or `render` group membership
- ✅ Better security posture when render nodes are available
- ✅ Maximum compatibility (works regardless of which group user is in)
- ✅ Clear error messages if neither works (suggests checking group membership)

**No urgent security issues** - current hybrid approach provides good security with maximum compatibility.

## Implementation: Enhanced Device Selection

**Current implementation (implemented):**

The renderer now tries both render nodes and card nodes:

1. **First, try render nodes** (`renderD128`, `renderD129`, etc.)
   - More secure (render-only access)
   - Requires `render` group membership
   - Preferred option

2. **Fall back to card nodes** (`card0`, `card1`, etc.) if render nodes don't work
   - Works with `video` group membership
   - Still secure for our use case (read-only framebuffer access)

3. **How it works:**
   - Iterates through `/dev/dri/` directory
   - For each device matching the prefix, tries to open it
   - Checks if the device has the framebuffer ID via `drmModeGetFB()`
   - First device that successfully returns framebuffer info is used

**Benefits:**
- ✅ Works with either `video` or `render` group membership
- ✅ Prefers more secure render nodes when available
- ✅ Graceful fallback to card nodes
- ✅ Better user experience (no need to check which group they're in)
- ✅ Clear error messages if neither works (suggests checking group membership)

