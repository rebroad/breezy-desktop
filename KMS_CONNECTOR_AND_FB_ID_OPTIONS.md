# KMS Connectors vs Userspace Virtual Outputs & Framebuffer ID Passing Options

## Part 1: Advantages of KMS Connectors (Option A)

### Current Problem
The renderer (`x11/renderer/drm_capture.c`) currently tries to find virtual outputs by:
1. Opening DRM device directly
2. Enumerating connectors via `drmModeGetResources()`
3. Looking for connector with name "XR-0" via DRM properties

This **won't work** with userspace-only virtual outputs because:
- Virtual outputs don't appear in `drmModeGetResources()`
- They have no `drmModeConnectorPtr` (it's `NULL`)
- They exist only in Xorg's RandR layer, not in the DRM kernel

### Advantages of KMS Connectors (Option A)

#### ✅ **Renderer Compatibility**
- Renderer can find virtual outputs via standard DRM enumeration
- No special IPC mechanism needed - works like any physical display
- Can use `drmModeGetConnector()`, `drmModeGetCrtc()`, `drmModeGetFB()` directly
- Framebuffer ID naturally available via CRTC → framebuffer query

#### ✅ **Standard DRM Tooling**
- Visible in `modetest`, `drm_info`, `igt` (Intel Graphics Test)
- Can debug with standard DRM debugging tools
- Appears in kernel's view of the system (`/sys/class/drm/`)
- Better for debugging issues with virtual displays

#### ✅ **Architectural Consistency**
- Virtual outputs look exactly like physical displays to all DRM code
- Can use DRM atomic modesetting APIs naturally
- Better integration with other DRM clients (compositors, VNC servers, etc.)
- Follows the standard DRM/KMS model that all graphics code expects

#### ✅ **Kernel-Level Management**
- Framebuffer allocation managed by kernel (better resource tracking)
- Kernel can enforce constraints and limits
- Better for multi-GPU scenarios (kernel handles device selection)
- More robust resource management

#### ✅ **Future-Proofing**
- If Wayland compositors want to use virtual outputs, they naturally work
- Compatible with future DRM features (HDR, variable refresh rate, etc.)
- Standard path for virtual display implementations (e.g., virtio-gpu, VNC)

### Disadvantages of KMS Connectors (Option A)

#### ❌ **Kernel Changes Required**
- Need to modify DRM driver (amdgpu, intel, nouveau, etc.)
- Or create new DRM helper module (like `drm_virt` or similar)
- Requires kernel development expertise
- May need upstream acceptance (or maintain out-of-tree patch)

#### ❌ **Complexity & Maintenance**
- More complex implementation (kernel + userspace)
- Longer development time
- Harder to debug (kernel crashes vs userspace crashes)
- Requires kernel rebuild/reload for changes

#### ❌ **Distribution Challenges**
- Users need custom kernel or kernel modules
- May conflict with distribution kernels
- Harder to package and distribute
- More setup complexity for end users

#### ❌ **May Be Overkill**
- Current userspace-only approach works fine for X11
- We can solve the framebuffer ID problem without kernel changes
- Userspace approach is faster to iterate on

### Recommendation for KMS Connectors

**For initial implementation**: Stick with Option B (userspace-only). It's sufficient for X11, faster to develop, and easier to distribute.

**Consider Option A if:**
- We need Wayland support (Wayland compositors expect KMS connectors)
- We want better debugging tools
- We need multi-GPU support
- We want to upstream the feature to mainline kernel
- We encounter limitations with userspace-only approach

---

## Part 2: Options for Passing Framebuffer ID to Renderer

### Current Situation

The renderer needs:
- Framebuffer ID (to query via `drmModeGetFB()`)
- Or directly: DRM handle (to export via `drmPrimeHandleToFD()`)
- Or directly: DMA-BUF file descriptor (to import via EGL)

The Xorg driver has:
- `vout->framebuffer_id` (DRM framebuffer ID)
- `vout->framebuffer_bo` (DRM buffer object with handle)
- `drmmode_xr_export_framebuffer_to_dmabuf()` function (exports to DMA-BUF FD)

### Option 1: RandR Property on XR-0 Output

**Mechanism:**
- Add a RandR output property `XR_FB_ID` (or `FRAMEBUFFER_ID`) on XR-0
- Property contains the DRM framebuffer ID as an integer
- Renderer queries property via X11 protocol or `xrandr --props`

**Implementation:**
```c
// In drmmode_xr_virtual.c
#define XR_FB_ID_PROPERTY "FRAMEBUFFER_ID"

// When creating virtual output, set property
RRConfigureOutputProperty(randr_output, XR_FB_ID_PROPERTY, 
                          FALSE, FALSE, TRUE, 1, &vout->framebuffer_id);

// When framebuffer changes (mode switch), update property
RROutputChangeProperty(randr_output, XR_FB_ID_PROPERTY, 
                       XA_INTEGER, 32, PropModeReplace, 1, &vout->framebuffer_id);
```

**Renderer side:**
```c
// Query via XRandR extension
XRROutputProperty *prop = XRRQueryOutputProperty(display, output, 
    XInternAtom(display, "FRAMEBUFFER_ID", False));
uint32_t fb_id = *(uint32_t*)prop->values;
```

**Pros:**
- ✅ Standard X11/RandR mechanism (already using RandR for XR-Manager)
- ✅ Easy to query via X11 protocol (`xrandr --props`, XRandR extension)
- ✅ Integrated with existing virtual output infrastructure
- ✅ No additional IPC mechanism needed
- ✅ Property updates automatically when framebuffer changes (mode switch)
- ✅ Already have property infrastructure in place (`XR_WIDTH`, `XR_HEIGHT`, etc.)

**Cons:**
- ❌ Renderer needs X11 connection (but it already uses X11 for GLX/EGL)
- ❌ Property queries have some overhead (but minimal, and only on init/mode change)
- ❌ Need to keep property in sync with framebuffer (but automatic via property update)

**Recommendation: ⭐⭐⭐⭐⭐ Best option** - Clean, standard, integrated with existing infrastructure.

---

### Option 2: Shared Memory (like IMU data)

**Mechanism:**
- Create shared memory file `/dev/shm/breezy_xr_fb_info`
- Write framebuffer ID, width, height, stride, format periodically
- Renderer reads from shared memory

**Implementation:**
```c
// In Xorg driver (shared with renderer via header file)
struct XRFramebufferInfo {
    uint32_t fb_id;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;  // DRM format code
    uint64_t modifier;
    uint8_t parity;  // Simple checksum
};

// Write to shm when framebuffer changes
shm_fd = shm_open("/breezy_xr_fb_info", O_CREAT | O_RDWR, 0666);
ftruncate(shm_fd, sizeof(XRFramebufferInfo));
info = mmap(..., shm_fd, ...);
info->fb_id = vout->framebuffer_id;
info->parity = calculate_parity(info);
```

**Pros:**
- ✅ Very fast (no syscalls for reading)
- ✅ Already using shm for IMU data (consistent pattern)
- ✅ No X11 dependency (renderer can read independently)
- ✅ Simple protocol

**Cons:**
- ❌ Need synchronization mechanism (when does renderer read? how to detect changes?)
- ❌ Potential race conditions (framebuffer changes while reading)
- ❌ Need to handle stale data (what if Xorg crashes?)
- ❌ Less integrated with existing RandR infrastructure
- ❌ Requires custom protocol (not standard)

**Recommendation: ⭐⭐⭐ Good for high-frequency updates, but overkill for framebuffer ID**

---

### Option 3: Unix Domain Socket / Named Pipe

**Mechanism:**
- Xorg driver listens on Unix socket `/tmp/breezy-xr-fb-info` or similar
- Renderer connects and requests framebuffer info
- Xorg sends framebuffer ID, handle, or DMA-BUF FD

**Implementation:**
```c
// Xorg side: socket server
int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
listen(server_fd, 1);
int client_fd = accept(server_fd, NULL, NULL);
write(client_fd, &vout->framebuffer_id, sizeof(uint32_t));
```

**Pros:**
- ✅ Clean request/response model
- ✅ Can send DMA-BUF FD directly via `SCM_RIGHTS` (socket file descriptor passing)
- ✅ Can handle multiple renderer connections
- ✅ Bidirectional (renderer can also send requests)

**Cons:**
- ❌ More complex implementation (socket server/client code)
- ❌ Need to handle connection management (what if renderer crashes? reconnect?)
- ❌ Additional IPC overhead (though minimal)
- ❌ Requires coordination between Xorg and renderer startup order

**Recommendation: ⭐⭐⭐⭐ Good if we want to pass DMA-BUF FD directly, but property approach is simpler**

---

### Option 4: File-Based (like `/sys/class/drm/`)

**Mechanism:**
- Write framebuffer ID to file: `/tmp/breezy-xr-0-fb-id` or `/run/breezy-xr-0-fb-id`
- Renderer reads file when needed
- Xorg updates file when framebuffer changes

**Implementation:**
```c
// Xorg side: write file
FILE *f = fopen("/run/breezy-xr-0-fb-id", "w");
fprintf(f, "%u\n", vout->framebuffer_id);
fclose(f);

// Renderer side: read file
FILE *f = fopen("/run/breezy-xr-0-fb-id", "r");
fscanf(f, "%u", &fb_id);
fclose(f);
```

**Pros:**
- ✅ Simple implementation
- ✅ Easy to debug (just `cat /run/breezy-xr-0-fb-id`)
- ✅ No X11 dependency
- ✅ Works even if Xorg isn't running (stale data, but handleable)

**Cons:**
- ❌ Filesystem overhead (open/read/close syscalls)
- ❌ Need to handle file locking or atomic writes
- ❌ Potential race conditions (file written while reading)
- ❌ Less integrated with Xorg infrastructure
- ❌ Need to clean up files on shutdown

**Recommendation: ⭐⭐ Simple but less elegant than property approach**

---

### Option 5: D-Bus Interface

**Mechanism:**
- Xorg driver exposes D-Bus interface: `org.breezy.XR.Framebuffer`
- Method: `GetFramebufferID(output_name)` returns framebuffer ID
- Renderer calls method via D-Bus

**Implementation:**
```c
// Xorg side: D-Bus service (would need GObject/D-Bus integration)
// Method handler
static gboolean
handle_get_framebuffer_id(..., const gchar *output_name, guint32 *fb_id, ...) {
    xr_virtual_output_ptr vout = find_virtual_output(output_name);
    *fb_id = vout->framebuffer_id;
    return TRUE;
}
```

**Pros:**
- ✅ Standard system-wide IPC mechanism
- ✅ Well-documented and supported
- ✅ Can handle multiple clients
- ✅ Type-safe (via D-Bus introspection)

**Cons:**
- ❌ D-Bus dependency (adds complexity)
- ❌ Requires GObject/D-Bus integration in Xorg driver (non-trivial)
- ❌ Overhead (D-Bus message serialization/deserialization)
- ❌ May be overkill for simple framebuffer ID passing

**Recommendation: ⭐⭐ Overkill for this use case**

---

### Option 6: Direct DMA-BUF FD Passing (via RandR Property or Socket)

**Mechanism:**
- Export DMA-BUF FD in Xorg driver using `drmmode_xr_export_framebuffer_to_dmabuf()`
- Pass FD to renderer via:
  - **6a:** Unix socket with `SCM_RIGHTS` (file descriptor passing)
  - **6b:** Inherit via environment variable + file descriptor inheritance (complex)

**Implementation (6a - Socket):**
```c
// Xorg side: send FD via socket
struct msghdr msg = {0};
struct cmsghdr *cmsg;
char buf[CMSG_SPACE(sizeof(int))];
msg.msg_control = buf;
msg.msg_controllen = sizeof(buf);
cmsg = CMSG_FIRSTHDR(&msg);
cmsg->cmsg_level = SOL_SOCKET;
cmsg->cmsg_type = SCM_RIGHTS;
cmsg->cmsg_len = CMSG_LEN(sizeof(int));
*(int*)CMSG_DATA(cmsg) = dmabuf_fd;
sendmsg(socket_fd, &msg, 0);
```

**Pros:**
- ✅ Most efficient (renderer gets DMA-BUF FD directly, no extra export step)
- ✅ Zero-copy path (FD → EGL import, no intermediate queries)
- ✅ Works with Option 3 (Unix socket) naturally

**Cons:**
- ❌ File descriptor passing is complex (need socket with `SCM_RIGHTS`)
- ❌ FD lifetime management (what if renderer crashes? FD leaks)
- ❌ Less standard (can't easily query via `xrandr`)
- ❌ Need to handle FD invalidation when framebuffer changes

**Recommendation: ⭐⭐⭐⭐ Good for optimization, but combine with Option 1 (property for ID) for flexibility**

---

## Recommended Approach: Hybrid Solution

### Primary: RandR Property (Option 1)

Use RandR property `FRAMEBUFFER_ID` on XR-0 output as the primary mechanism:

1. **Simple and standard** - works with existing infrastructure
2. **Easy to query** - renderer can use XRandR extension or `xrandr`
3. **Automatic updates** - property updates when framebuffer changes
4. **Integrated** - fits naturally with `XR_WIDTH`, `XR_HEIGHT` properties

### Note on Performance: Socket is NOT an Optimization

**Important:** A Unix socket approach would **NOT** improve performance. The current implementation exports the DMA-BUF FD on every frame, which is inefficient. The optimization is to **export once and reuse the FD** until the framebuffer changes, regardless of how the initial FD is obtained.

See `DMA_BUF_PERFORMANCE_ANALYSIS.md` for detailed performance analysis.

**Both approaches (property + export, or socket + export) can be optimized the same way:**
- Export DMA-BUF FD **once** during initialization
- Reuse the same FD for all frames
- Only re-export when framebuffer changes (detected via error from `drmModeGetFB()`)

The socket approach would add complexity without performance benefit, since it would still require per-frame communication if used incorrectly, or the same "export once, reuse" pattern if used correctly.

### Implementation Priority

1. **Phase 1:** Implement RandR property `FRAMEBUFFER_ID` (Option 1)
2. **Phase 2:** Update renderer to query property and use `drmModeGetFB()` + `drmPrimeHandleToFD()`
3. **Phase 3 (optional):** Add socket-based DMA-BUF FD passing for optimization

---

## Summary

**KMS Connectors (Option A):**
- Would solve renderer compatibility naturally
- Better debugging and tooling
- But requires kernel changes (complex, distribution challenges)
- **Recommendation:** Stick with Option B for now, consider Option A later if needed

**Framebuffer ID Passing:**
- **Best option:** RandR property `FRAMEBUFFER_ID` (Option 1)
  - Standard, integrated, easy to implement
  - Renderer can query via XRandR extension
  - Automatic updates when framebuffer changes
  - **Performance note:** The real optimization is exporting DMA-BUF FD once and reusing it (see `DMA_BUF_PERFORMANCE_ANALYSIS.md`)
  - Unix socket approach would not improve performance and adds complexity

