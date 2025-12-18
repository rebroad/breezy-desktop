---
name: XFCE4 AR Support via Xorg Virtual XR Connector
overview: ""
todos:
  - id: design-modesetting-xr-connector
    content: Design Xorg modesetting virtual XR connector and AR mode semantics (how XR-0 appears in RandR, how AR mode hides the physical XR connector).
    status: pending
  - id: impl-modesetting-xr-connector
    content: Extend Xorg modesetting driver to add a virtual XR connector output with modes, off-screen framebuffer, and AR mode that toggles visibility of physical vs virtual XR outputs.
    status: pending
    dependencies:
      - design-modesetting-xr-connector
  - id: wire-breezy-xfce-backend
    content: Update Breezy XFCE4 backend to discover the virtual XR connector via XRandR and treat it as the virtual desktop plane for AR.
    status: pending
    dependencies:
      - impl-modesetting-xr-connector
  - id: impl-xfce4-renderer
    content: Complete the XFCE4 3D renderer to capture from the virtual XR connector surface, apply IMU-driven 3D transforms, and render to the XR output at the glasses refresh rate.
    status: pending
    dependencies:
      - wire-breezy-xfce-backend
  - id: integrate-breezy-ui
    content: Hook the XFCE4 backend and AR mode into Breezy’s UI and lifecycle (start/stop AR, toggle AR mode, restore normal 2D mapping).
    status: pending
    dependencies:
      - impl-xfce4-renderer
  - id: test-and-docs
    content: Test end-to-end scenarios (normal 2D, extended, AR) and update Breezy and XFCE4-specific documentation for the new Xorg-based XR path.
    status: pending
    dependencies:
      - integrate-breezy-ui
---

# XFCE4 AR Support via Xorg Virtual XR Connector

## Overview

Implement full Breezy Desktop support for XFCE4 on X11 by extending the Xorg modesetting driver with a RandR‑visible virtual XR connector and AR mode, and wiring Breezy’s XFCE4 backend and 3D renderer to use it. XFCE’s existing display tools should see the virtual connector as a normal monitor for layout, while Breezy’s renderer owns the actual AR/3D presentation on the XR glasses.

## Plan

### 1. Understand existing architectures (reference only)

- **Mutter / GNOME**: Skim how Mutter models monitors and virtual displays (e.g. `MetaMonitorManager`, logical monitors, XR‑style outputs) to inform conceptual design, without copying code.
- **Xorg modesetting driver**: Study `hw/xfree86/drivers/modesetting` to see how connectors, CRTCs, and RandR outputs are represented and exposed to clients.
- **Current Breezy GNOME/KDE path**: Revisit how Breezy interacts with Mutter/KWin (IMU feed, virtual displays, shaders) so we can match capabilities on XFCE4.

### 2. Design the Xorg virtual XR connector and AR mode

- Specify a **virtual connector** (e.g. `XR-0`) in the modesetting driver:
- Always present (or enabled via Xorg config option).
- Has its own RandR output and mode list.
- Define **AR mode semantics**:
- Normal mode: physical glasses output appears as a regular XRandR monitor.
- AR mode: physical glasses output is hidden from the 2D XRandR map; the virtual XR connector is driven by Breezy’s 3D renderer.
- Decide how the driver will back the virtual connector:
- Off‑screen framebuffer (system or GPU memory).
- Mechanism for exposing its contents to Breezy (e.g. X pixmap, shared memory handle, or documented capture path).

### 3. Implement virtual XR connector in the modesetting driver

- Fork/branch the Xorg `modesetting` driver in the `xserver` tree.
- Add a **virtual connector object** and corresponding RandR output:
- Hook into connector enumeration so `xrandr` reports `XR-0`.
- Implement mode handling for `XR-0` (report available modes, accept mode set requests).
- Implement **CRTC / scanout behaviour** for `XR-0`:
- Instead of programming hardware, render into the off‑screen buffer associated with the virtual connector.
- Ensure the buffer respects the current mode (resolution, refresh), ready for Breezy to capture.
- Add configuration knobs (e.g. in `xorg.conf` or driver options) to enable/disable the virtual XR connector and AR mode.

### 4. Integrate AR mode and hiding the physical XR output

- Extend the modesetting driver’s RandR code to support **two representations**:
- Physical XR connector visible as a monitor in normal 2D mode.
- Virtual XR connector visible, physical hidden, in AR mode.
- Ensure XFCE’s display tool sees only the appropriate outputs based on mode:
- In AR mode, users arrange the virtual XR connector like any other monitor.
- Provide a control path (X property, RandR property, or simple CLI) to toggle AR mode that Breezy or the user can call.

### 5. Wire Breezy’s XFCE backend to the virtual XR connector

- Update `breezy-desktop`’s XFCE4 backend to:
- Discover the virtual XR connector via XRandR (`XR-0`).
- Read its geometry and placement, treating it as the **virtual desktop plane** for AR.
- Define how Breezy maps XFCE’s **2D layout** into 3D:
- The virtual connector rectangle becomes the content plane for AR rendering.
- Breezy’s UI continues to manage AR‑specific properties (distance, curvature, follow mode) independently of XFCE.

### 6. Implement / complete the XFCE4 3D renderer

- Use the existing skeleton in [`breezy-desktop/xfce4/renderer/breezy_xfce4_renderer.py`](breezy-desktop/xfce4/renderer/breezy_xfce4_renderer.py) (or C/C++ equivalent) to:
- Implement an **X11 capture thread** that reads from the virtual XR connector’s framebuffer or associated surface.
- Implement a **render thread** that:
    - Reads IMU data from `/dev/shm/breezy_desktop_imu`.
    - Applies 3D transforms using GLSL shaders and math ported from GNOME/KWin (`virtualdisplayeffect.js`, `math.js`).
    - Renders to the XR connector at the glasses refresh rate (60/72Hz) with VSync.
- Ensure the renderer can run independently of GNOME/KWin and is fully driven by Breezy on XFCE4.

### 7. Integrate with Breezy UI and lifecycle

- Ensure `extensionsmanager.py` and `virtualdisplaymanager.py` load the XFCE4 backend when running under XFCE.
- Implement lifecycle hooks so that:
- Starting an AR session on XFCE4 enables the virtual XR connector (and AR mode if needed).
- Stopping AR restores the normal 2D mapping (physical XR monitor visible again, virtual XR connector disabled).
- Add configuration options in Breezy’s UI to:
- Select AR vs normal mode.
- Choose how the virtual connector is positioned (via XFCE display tool) and how it maps into AR space.

### 8. Build, test, and iterate

- Build and install the patched Xorg driver in a controlled environment (e.g. alternate Xorg binary or custom package) to avoid breaking the system X.
- Test scenarios:
- Single‑monitor + XR glasses in normal 2D mode.
- Extended desktop with virtual XR connector visible in XFCE’s display tool.
- AR mode: physical XR monitor hidden, virtual XR connector arranged, Breezy rendering 3D desktops in the glasses.
- Measure performance and latency; tune capture and rendering (e.g. PBOs, texture upload strategies).
