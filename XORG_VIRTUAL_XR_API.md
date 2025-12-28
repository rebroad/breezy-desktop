# X11 Xorg Virtual Display Connector & AR Mode Design

## Goals

- Provide a generic **virtual display connector** system for X11/Xorg that supports multiple use cases:
  - **XR (Extended Reality) displays**: For AR/VR-style glasses (XREAL, VITURE, etc.)
  - **Remote streaming displays**: For streaming desktop content to remote devices (e.g., Raspberry Pi clients)
  - **Other virtual display uses**: Any use case requiring a virtual monitor that appears in RandR
- Virtual displays are managed through a **control output** (`VIRTUAL-MANAGER`) and can be created with arbitrary names
- For XR use case specifically, add an **AR mode** in the Xorg modesetting driver that:
  - Hides the physical XR connector from the 2D XRandR map.
  - Lets Breezy's 3D renderer drive the XR connector exclusively.

## High-level Architecture

```mermaid
flowchart LR
  kernelDRM[KernelDRM/amdgpu]
  xorg[Xorg+Modesetting]
  virtMgr[VIRTUAL-MANAGER\ncontrol output]
  virt0[Virtual Display-0\n(e.g., XR-0, REMOTE-0)]
  virt1[Virtual Display-1\n(arbitrary name)]
  physXR[PhysicalXR connector]
  randrClients[RandR clients\n(display tool, apps)]
  breezy[Breezy X11 backend\n+ 3D renderer]
  remoteClient[Remote Client\n(Raspberry Pi, etc.)]

  kernelDRM --> xorg
  xorg --> physXR
  xorg --> virtMgr
  virtMgr --> virt0
  virtMgr --> virt1
  xorg <---> randrClients
  xorg <---> breezy
  virt1 -.streams to.-> remoteClient
```

- `VIRTUAL-MANAGER` is a **control output** (always disconnected, non-desktop) used to manage virtual displays
- Virtual displays (e.g., `XR-0`, `REMOTE-0`, `STREAM-0`) are **synthetic connectors/outputs** exposed by the modesetting driver
- Virtual display names are **arbitrary** - users can choose meaningful names based on use case
- RandR clients (display settings tools, apps) see virtual displays as normal monitors
- Different use cases:
  - **XR displays**: Breezy reads geometry via RandR, captures content, renders AR content to physical XR connector
  - **Remote streaming**: Content from virtual display can be streamed (e.g., via PipeWire) to remote clients
  - **Other uses**: Any application needing a virtual monitor

## Xorg modesetting design

### Data Structures

In `hw/xfree86/drivers/modesetting/driver.h`:

- Extend `modesettingRec` to track virtual display state:

```c
typedef struct _modesettingRec {
    ...
    Bool virtual_displays_enabled;    /* virtual display system enabled */
    Bool ar_mode;                     /* AR mode: hide physical XR, show virtual XR displays */
    xf86OutputPtr virtual_manager_output; /* VIRTUAL-MANAGER control output (always disconnected) */
    /* List/array of virtual display outputs (dynamically created) */
    /* Each virtual display has its own pixmap and FB id */
    /* Structure to track individual virtual displays */
    struct {
        xf86OutputPtr output;         /* xf86Output for this virtual display */
        PixmapPtr pixmap;             /* Off-screen pixmap for content */
        uint32_t fb_id;               /* DRM FB id for zero-copy capture */
        char *name;                   /* Display name (e.g., "XR-0", "REMOTE-0") */
    } virtual_displays[MAX_VIRTUAL_DISPLAYS];
    int num_virtual_displays;
} modesettingRec, *modesettingPtr;
```

- Add helper predicates:

```c
static inline Bool
ms_virtual_displays_enabled(modesettingPtr ms)
{
    return ms->virtual_displays_enabled;
}

static inline Bool
ms_ar_mode_enabled(modesettingPtr ms)
{
    return ms->virtual_displays_enabled && ms->ar_mode;
}
```

### Virtual Display Connector & Output Creation

Connector enumeration currently happens in `drmmode_output_init(...)` using KMS connectors from `drmModeResPtr mode_res`.

We will:

- **Option A (not implemented, future enhancement):** Add a small KMS "virtual connector" in the kernel/DRM layer (advanced, out-of-scope initially).
- **Option B (current implementation):** Inject an extra `xf86OutputPtr` for VIRTUAL-MANAGER and virtual display outputs purely in userspace:
  - After probing real connectors, create an extra `xf86Output` named `"VIRTUAL-MANAGER"` (control output, always disconnected, non-desktop).
  - Dynamically create `xf86Output` instances for virtual displays via RandR properties.
  - Virtual display names are **arbitrary** - examples: `XR-0`, `REMOTE-0`, `STREAM-0`, `MY-VIRTUAL-DISPLAY`
  - These outputs have no corresponding KMS connector (`mode_output = NULL`).
  - They are synthetic outputs that exist only in Xorg's RandR layer.

Virtual displays are created dynamically via RandR properties on the `VIRTUAL-MANAGER` output:

- **CREATE_VIRTUAL_OUTPUT property**: Creates a new virtual display with the specified name, width, height, and optional refresh rate
  - Format: `"NAME:WIDTH:HEIGHT[:REFRESH]"`
  - Example: `"XR-0:1920:1080:60"` or `"REMOTE-0:2560:1440"` (refresh defaults to 60)
  - The name can be any valid output name (not restricted to "XR-*")

- **DELETE_VIRTUAL_OUTPUT property**: Deletes a virtual display
  - Format: `"NAME"`
  - Example: `"XR-0"`

Virtual displays will supply their mode list via the existing `drmmode_output_funcs.mode_get`-like callback or a custom implementation that:

- Returns a fixed set of modes (e.g. 3840x2160, 2560x1440, 1920x1080).
- Does **not** depend on EDID.
- Mode list is determined by the size specified during creation.

### AR Mode Semantics (XR Use Case)

**Important**: "AR mode" is **not a driver-level concept** - it's just Breezy Desktop's term for when the physical XR output is marked as `non-desktop`. Virtual outputs work like normal extended displays regardless.

- **Normal 2D mode:**
  - Physical XR connector (e.g. `DisplayPort-0` or similar) is visible in RandR as a normal desktop display (`non-desktop=false`).
  - Virtual XR displays (e.g., `XR-0`) work like any extended display:
    - Can be shown on regular physical displays (HDMI, DisplayPort, etc.) via panning/extended desktop
    - Can be positioned, extended, or mirrored like any other monitor
    - Moving mouse into virtual output (if panning enabled) causes it to be displayed on physical displays

- **"AR mode" (Breezy Desktop concept):**
  - Physical XR connector is **marked as non-desktop** in RandR:
    - RandR output property `non-desktop` set to `true` (standard RandR property).
    - This hides it from display settings tools (it remains accessible to Breezy's renderer).
  - Virtual XR display (e.g., `XR-0`) **continues to work as a normal extended display**:
    - Can still be shown on regular physical displays via panning/extended desktop
    - Can be captured via DMA-BUF by Breezy's renderer (independent operation)
    - Breezy's renderer processes the framebuffer and drives the physical XR connector via GPU APIs

Control mechanism (per-device, managed by Breezy Desktop):

- **Breezy Desktop manages AR mode per physical XR device** (not via a global property):
  1. **During calibration**: Physical XR device is visible as a normal desktop display.
  2. **After calibration completes**: Breezy Desktop:
     - Creates virtual XR display (e.g., `XR-0`) via `VIRTUAL-MANAGER` → automatically desktop.
     - Sets physical XR output's `non-desktop` property to `true` via RandR API.
     - Enables and positions the virtual XR display.
  3. **When disabling AR mode**: Breezy Desktop:
     - Sets physical XR output's `non-desktop` property to `false`.
     - Optionally disables or removes the virtual XR display.

- **RandR property on physical XR output**:
  - Use standard RandR `non-desktop` property (type: boolean).
  - Set via `RRChangeOutputProperty()` or `xrandr --output <PHYSICAL-XR> --set non-desktop true`.
  - This is per-output, not global - allows multiple XR devices with independent AR mode state.

**Note**:
- AR mode is per-device, not global. Each physical XR device can independently be in AR mode.
- Remote streaming displays and other virtual displays are unaffected by AR mode changes.
- The `non-desktop` property is a standard RandR property, not a custom XR-specific property.

### Off-Screen Buffer / Capture Path

For virtual displays we need a consistent captureable surface:

- **First implementation:**
  - Each virtual display has an off-screen **Pixmap** sized to its mode.
  - Associate a DRM FB with each pixmap if required for KMS.
  - Ensure the compositor/desktop painting code can blit or render the relevant desktop region into each pixmap each frame (this part can be refined later).

- **Capture strategies** (depending on use case):
  - **XR displays**: Breezy's renderer uses **zero-copy DMA-BUF capture**:
    - Queries `FRAMEBUFFER_ID` property from the virtual display via RandR.
    - Uses `drmModeGetFB()` to get framebuffer information from the DRM device.
    - Exports framebuffer handle as DMA-BUF file descriptor via `drmPrimeHandleToFD()`.
    - Imports DMA-BUF directly into OpenGL/EGL texture via `eglCreateImageKHR()` with `EGL_LINUX_DMA_BUF_EXT`.
    - **Zero-copy**: No CPU memory copy, framebuffer is accessed directly by GPU.
    - This is the same technique used by Mutter and modern compositors for zero-copy compositing.
  - **Remote streaming displays**: Content can be captured and streamed via:
    - PipeWire (for screen sharing to remote clients)
    - Direct framebuffer access via DMA-BUF (similar to XR capture)
    - Other capture mechanisms as needed

The DMA-BUF zero-copy capture method provides optimal performance with minimal CPU overhead, avoiding the need for XShm/XGetImage CPU-side copies.

## Breezy & X11 Integration (summary)

- Display settings tools:
  - See virtual displays as normal RandR outputs.
  - User arranges them relative to other monitors.
  - Display names appear as specified during creation (e.g., `XR-0`, `REMOTE-0`).

- Breezy X11 backend (XR use case):
  - **During calibration**: Physical XR device is visible as a normal desktop display.
  - **After calibration completes**:
    1. Creates virtual XR display (e.g., `XR-0`) via `VIRTUAL-MANAGER` (automatically desktop).
    2. Sets physical XR output's `non-desktop` property to `true` to hide it from display settings.
    3. Enables and positions the virtual XR display.
  - Uses XRandR to discover virtual XR displays (e.g., `XR-0`) and read their geometry and placement.
  - Treats the virtual display's rectangle as the 2D "virtual desktop plane" for AR.

- Breezy 3D renderer (XR use case):
  - Captures virtual XR display contents.
  - Combines them with IMU data.
  - Renders to the XR connector in AR space, respecting the layout configured through display settings tools.

- Remote streaming clients:
  - Can query virtual display properties via XRandR (just like a physical display).
  - Can receive streamed content from the virtual display via PipeWire or other streaming protocols.

## Communication Interface: Breezy ↔ Xorg

### Design Decision: Direct XRandR Communication

Breezy communicates **directly with Xorg** via the XRandR extension (no intermediate daemon needed):

- Breezy runs as a session service and can use XRandR/X11 APIs directly
- The modesetting driver reads RandR properties to control AR mode
- Simpler architecture: fewer moving parts, no IPC overhead

### RandR-Based Communication

**Client → Xorg** (via XRandR API):

1. **Create/delete virtual displays**:
   - Set `CREATE_VIRTUAL_OUTPUT` property on `VIRTUAL-MANAGER` output to create a virtual display
   - Format: `"NAME:WIDTH:HEIGHT[:REFRESH]"` (e.g., `"XR-0:1920:1080:60"` or `"REMOTE-0:2560:1440"`)
   - Set `DELETE_VIRTUAL_OUTPUT` property to delete a virtual display
   - Format: `"NAME"` (e.g., `"XR-0"`)

2. **Enable/disable virtual displays**:
   - Use `RRSetOutputPrimary()` or `RRSetCrtcConfig()` to enable/disable virtual displays
   - Create modes via `RRAddOutputMode()` if needed

3. **Set AR mode (XR use case) - per physical device**:
   - Set standard RandR `non-desktop` property (type: boolean) on the **physical XR output** (not on VIRTUAL-MANAGER)
   - Example: `RRChangeOutputProperty(physical_xr_output, "non-desktop", XA_INTEGER, 32, PropModeReplace, 1, &non_desktop)`
   - When `non-desktop=true`: Physical XR connector is hidden from display settings tools (remains accessible to renderers)
   - When `non-desktop=false`: Physical XR connector appears as a normal desktop display
   - **Note**: AR mode is managed per physical XR device, not globally

4. **Query connector state**:
   - Use `RRGetOutputInfo()` to get geometry, position, enabled status for virtual displays
   - Read `non-desktop` property via `RRGetOutputProperty()` on physical XR outputs
   - Read `FRAMEBUFFER_ID` property on virtual displays for zero-copy DMA-BUF capture (XR use case)

**Xorg modesetting driver → Clients** (via RandR properties):

- Virtual displays expose `FRAMEBUFFER_ID` property for zero-copy DMA-BUF capture (XR use case)
- Physical outputs support standard RandR `non-desktop` property (per-output, not global)
- The driver respects the `non-desktop` property set by clients (Breezy Desktop) - no special driver logic needed

### Comparison with Mutter/KWin

| Feature | Mutter | KWin | Xorg (our design) |
|---------|--------|------|-------------------|
| Virtual Display API | `RecordVirtual()` via D-Bus | `AddVirtualDisplay()` via D-Bus | Direct XRandR API calls |
| Display Discovery | Via Mutter's monitor manager | Via KWin's output backend | Via RandR (`RRGetOutputInfo()`) |
| Content Capture | PipeWire stream | Direct compositor access | X11 pixmap/shm from off-screen buffer |
| AR Mode Control | Built into Mutter | Built into KWin | RandR property `AR_MODE` |

### Breezy Integration

Breezy's X11 backend (`x11/src/x11_backend.py`) will use Python XRandR bindings (e.g., `python-xlib` or `xcb`) or subprocess to `xrandr`:

1. **Check availability**:
   ```python
   def is_available(self):
       # Check if VIRTUAL-MANAGER output exists via xrandr
       result = subprocess.run(['xrandr', '--listoutputs'],
                              capture_output=True, text=True)
       return 'VIRTUAL-MANAGER' in result.stdout
   ```

2. **Create virtual display** (XR use case):
   ```python
   def create_virtual_display(self, width, height, framerate, name="XR-0"):
       # Create virtual display via VIRTUAL-MANAGER CREATE_VIRTUAL_OUTPUT property
       # Format: "NAME:WIDTH:HEIGHT:REFRESH"
       # Name can be anything - "XR-0", "REMOTE-0", "STREAM-0", etc.
       create_cmd = f"{name}:{width}:{height}:{framerate}"
       subprocess.run(['xrandr', '--output', 'VIRTUAL-MANAGER',
                      '--set', 'CREATE_VIRTUAL_OUTPUT', create_cmd])
       return name  # Returns the output name (e.g., "XR-0")
   ```

3. **Enable AR mode** (XR use case - per physical device):
   ```python
   def enable_ar_mode(self, physical_xr_output_name: str):
       # Set non-desktop property on physical XR output to hide it from display settings
       # This is done AFTER creating the virtual XR display
       subprocess.run(['xrandr', '--output', physical_xr_output_name,
                      '--set', 'non-desktop', 'true'])
       # Virtual XR display is automatically desktop (created as normal output)
   ```
   
   **Note**: AR mode is managed **per physical XR device** (not global).
   - Set `non-desktop=true` on the **physical XR output** (e.g., `DisplayPort-0`)
   - Virtual XR displays are automatically desktop outputs when created
   - Non-XR virtual displays (e.g., remote streaming displays) are unaffected

4. **Disable AR mode**:
   ```python
   def disable_ar_mode(self, physical_xr_output_name: str):
       # Restore physical XR output as desktop display
       subprocess.run(['xrandr', '--output', physical_xr_output_name,
                      '--set', 'non-desktop', 'false'])
       # Optionally disable/remove virtual XR display
   ```

4. **Create remote streaming display** (example):
   ```python
   def create_remote_display(self, width, height, framerate=60):
       # Create a virtual display for remote streaming
       name = "REMOTE-0"
       create_cmd = f"{name}:{width}:{height}:{framerate}"
       subprocess.run(['xrandr', '--output', 'VIRTUAL-MANAGER',
                      '--set', 'CREATE_VIRTUAL_OUTPUT', create_cmd])
       return name
   ```

**Note**: For better performance and reliability, the backend should eventually use direct XRandR API calls (via `python-xlib` or `xcb`) instead of subprocess calls, but subprocess works for initial implementation.

### Virtual Display Naming Conventions

While virtual display names are **arbitrary**, suggested conventions:

- **XR displays**: `XR-0`, `XR-1`, etc. (for AR/VR use cases)
- **Remote streaming**: `REMOTE-0`, `REMOTE-1`, `STREAM-0`, etc.
- **Custom uses**: Any meaningful name that identifies the purpose

The name is purely for identification and does not affect functionality - all virtual displays behave the same way regardless of name.


