# XFCE4 Xorg Virtual XR Connector & AR Mode Design

## Goals

- In this document, **XR** means **Extended Reality** (AR/VR-style glasses such as XREAL, VITURE, etc.).
- Provide Breezy Desktop with a **RandR-visible virtual XR connector** (`XR-0`) on X11, so:
  - XFCE’s display tool can position/resize it like a normal monitor.
  - Breezy can treat it as the “virtual desktop plane” for AR.
- Add an **AR mode** in the Xorg modesetting driver that:
  - Hides the physical XR connector from the 2D XRandR map.
  - Lets Breezy’s 3D renderer drive the XR connector exclusively.

## High-level Architecture

```mermaid
flowchart LR
  kernelDRM[KernelDRM/amdgpu]
  xorg[Xorg+Modesetting]
  xr0[XR-0 virtual connector]
  physXR[PhysicalXR connector]
  xfceRandr[XFCE RandR clients\n(display tool, apps)]
  breezy[Breezy XFCE backend\n+ 3D renderer]

  kernelDRM --> xorg
  xorg --> physXR
  xorg --> xr0
  xorg <---> xfceRandr
  xorg <---> breezy
```

- `XR-0` is a **virtual connector/output** exposed by the modesetting driver.
- RandR clients (XFCE display tool, apps) see `XR-0` as a normal monitor.
- Breezy:
  - Reads `XR-0` geometry/placement via RandR.
  - Captures its content from an associated off-screen buffer / pixmap.
  - Renders AR content to the real XR connector when AR mode is active.

## Xorg modesetting design

### Data Structures

In `hw/xfree86/drivers/modesetting/driver.h`:

- Extend `modesettingRec` to track XR state:

```c
typedef struct _modesettingRec {
    ...
    Bool xr_enabled;          /* virtual XR connector present */
    Bool xr_ar_mode;          /* AR mode: hide physical XR, use XR-0 */
    drmModeConnectorPtr xr_koutput; /* optional KMS-level representation */
    xf86OutputPtr xr_output;  /* xf86Output for XR-0 */
    /* Off-screen buffer / pixmap for XR-0 content (to be defined) */
    PixmapPtr xr_pixmap;
    uint32_t xr_fb_id;        /* DRM FB id if needed */
} modesettingRec, *modesettingPtr;
```

- Add helper predicates:

```c
static inline Bool
ms_xr_is_enabled(modesettingPtr ms)
{
    return ms->xr_enabled;
}

static inline Bool
ms_xr_is_ar_mode(modesettingPtr ms)
{
    return ms->xr_enabled && ms->xr_ar_mode;
}
```

### XR-0 Connector & Output Creation

Connector enumeration currently happens in `drmmode_output_init(...)` using KMS connectors from `drmModeResPtr mode_res`.

We will:

- **Option A (preferred later):** Add a small KMS “virtual connector” in the kernel/DRM layer (advanced, out-of-scope initially).
- **Option B (initial):** Inject an extra `xf86OutputPtr` for XR-0 purely in userspace:
  - After probing real connectors, create an extra `xf86Output` named `"XR-0"`.
  - Mark it as `non_desktop` when AR mode is inactive if needed.

Pseudo-hook in modesetting screen init (in `driver.c` after real outputs are set up):

```c
static void
ms_init_xr_output(ScrnInfoPtr pScrn, drmmode_ptr drmmode)
{
    modesettingPtr ms = modesettingPTR(pScrn);
    xf86OutputPtr output;

    if (!ms->xr_enabled)
        return;

    output = xf86OutputCreate(pScrn, &drmmode_output_funcs, "XR-0");
    if (!output)
        return;

    /* No KMS connector: synthetic output with off-screen backing */
    output->mm_width  = 0;
    output->mm_height = 0;
    output->driver_private = NULL; /* or small private struct */

    output->possible_crtcs   = ~0; /* refine later */
    output->possible_clones  = 0;
    output->interlaceAllowed = TRUE;
    output->doubleScanAllowed = TRUE;

    ms->xr_output = output;

    /* Create RandR output if using dynamic outputs */
    output->randr_output = RROutputCreate(xf86ScrnToScreen(pScrn),
                                          output->name,
                                          strlen(output->name),
                                          output);
    if (output->randr_output) {
        /* Configure any XR-specific RandR properties later */
    }
}
```

XR-0 will supply its mode list via the existing `drmmode_output_funcs.mode_get`-like callback or a custom implementation that:

- Returns a fixed set of modes (e.g. 3840x2160, 2560x1440, 1920x1080).
- Does **not** depend on EDID.

### AR Mode Semantics

- **Normal 2D mode:**
  - Physical XR connector (e.g. `DisplayPort-0` or similar) is visible in RandR.
  - XR-0 may be disabled, or present but unused.
  - Glasses behave like a normal monitor in XFCE’s display tool.

- **AR mode:**
  - Physical XR connector is **hidden** from the 2D XRandR map:
    - RandR output flagged as non-desktop, disabled, or not enumerated to clients.
  - XR-0 is **enabled** and visible as a standard output.
  - Breezy’s renderer uses XR-0’s framebuffer as the AR “screen” and drives the real XR connector via GPU APIs.

Control mechanism:

- Implement a **RandR output property** on XR-0, e.g. `XR_AR_MODE` (boolean) or a separate screen property:

```c
Atom xr_ar_mode_atom; /* stored in modesettingRec */
```

- When `XR_AR_MODE` is set:
  - Toggle `ms->xr_ar_mode`.
  - Ensure:
    - Physical XR output is DPMS-off or otherwise hidden.
    - XR-0 RandR output is enabled.

### Off-Screen Buffer / Capture Path

For XR-0 we need a consistent captureable surface:

- **First implementation:**
  - Use an off-screen **Pixmap** (`ms->xr_pixmap`) sized to the XR-0 mode.
  - Associate a DRM FB (`ms->xr_fb_id`) with this pixmap if required for KMS.
  - Ensure the compositor/desktop painting code can blit or render the relevant desktop region into `ms->xr_pixmap` each frame (this part can be refined later).

- **Capture by Breezy:**
  - Breezy’s renderer can:
    - Use XShm/XGetImage on XR-0’s region.
    - Or, for better performance, use DRI/GLX/DRI3 to bind the pixmap as a texture and sample it directly.

Exact capture strategy can evolve independently of the modesetting driver, as long as XR-0’s contents are represented in a consistent pixmap/FB.

## Breezy & XFCE Integration (summary)

- XFCE’s display tool:
  - Sees XR-0 as a normal RandR output.
  - User arranges it relative to other monitors.

- Breezy XFCE backend:
  - Uses XRandR to discover XR-0 and read its geometry and placement.
  - Treats XR-0’s rectangle as the 2D “virtual desktop plane” for AR.

- Breezy 3D renderer:
  - Captures XR-0 contents.
  - Combines them with IMU data.
  - Renders to the XR connector in AR space, respecting the layout configured through XFCE’s display tool.


