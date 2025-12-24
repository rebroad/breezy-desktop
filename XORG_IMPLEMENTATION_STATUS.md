# Xorg Virtual XR Connector Implementation Status

## Overview

This document tracks the implementation status of the Xorg modesetting driver (`xserver/hw/xfree86/drivers/modesetting/drmmode_xr_virtual.c`) to support virtual XR outputs for XFCE4.

## Current Implementation Status

Based on code review of `drmmode_xr_virtual.c`:

### ✅ Completed

**Xorg Virtual XR Infrastructure**:
- XR-Manager control output creation and RandR integration
- Virtual XR output creation infrastructure (CREATE_XR_OUTPUT, DELETE_XR_OUTPUT)
- Virtual CRTC creation for virtual outputs (software-based CRTCs)
- Off-screen framebuffer/pixmap creation (GBM and dumb buffer support)
- AR mode logic (hide/show physical XR connector via AR_MODE property)
- Physical XR display detection via EDID and `non_desktop` marking
- RandR integration (outputs appear in `xrandr` and Display Settings)

**Breezy Desktop (GNOME)**:
- Breezy Desktop working on X11 under GNOME
- Mutter integration for 3D rendering
- DisplayConfig D-Bus interface support
- IMU integration via XRLinuxDriver

### 🚧 In Progress

#### 1. **Mode Handling for Virtual Outputs** (Required)

**What's needed:**
- Virtual outputs should support dynamic resolution changes via RandR properties (XR_WIDTH, XR_HEIGHT, XR_REFRESH)
- Mode lists for virtual outputs (fixed set of common resolutions: 1920x1080, 2560x1440, 3840x2160, etc.)
- Mode setting callbacks that update framebuffer size when mode changes

**Why it's needed:**
- Allows `breezy-desktop` to dynamically control the resolution of the virtual desktop
- Essential for adapting to different AR content and user preferences

**Reference:** `XORG_VIRTUAL_XR_API.md` lines 125-129

#### 2. **DRM Framebuffer Export for Zero-Copy Capture** (Required)

**What's needed:**
- Export virtual output framebuffers as DMA-BUF file descriptors
- Use `drmPrimeHandleToFD` or equivalent to expose framebuffers for capture
- Ensure framebuffers are in formats compatible with EGL DMA-BUF import (typically XRGB8888 or ARGB8888)

**Why it's needed:**
- The 3D renderer uses DMA-BUF zero-copy for optimal performance
- Current renderer expects to access framebuffers via DRM API (`xfce4/renderer/drm_capture.c`)
- Eliminates CPU-side pixel copying, reducing latency and improving performance

**Reference:** `xfce4/renderer/DMA_BUF_OPTIMIZATION.md`

#### 3. **XFCE4 Backend Integration** (Required)

**What's needed:**
- Calibration detection and monitoring from XRLinuxDriver
- XR-0 creation via XR-Manager API after calibration completes
- AR mode enablement after XR-0 creation
- Renderer process management (start/stop)
- Integration with Breezy Desktop UI

**Why it's needed:**
- The XFCE4 backend (`xfce4/src/xfce4_backend.py`) needs to orchestrate the full workflow
- Must coordinate between XRLinuxDriver, Xorg virtual outputs, and the 3D renderer

**Reference:** `xfce4/src/xfce4_backend.py` - Backend implementation

#### 4. **Integration Testing** (Required)

**What's needed:**
- End-to-end testing of the complete workflow
- Validation that all components work together correctly
- Performance testing and optimization

## Implementation Order (Recommended)

1. **Mode Handling** - Required for dynamic resolution changes
2. **DRM Framebuffer Export** - Required for zero-copy capture (DMA-BUF)
3. **XFCE4 Backend Integration** - Required for end-to-end workflow
4. **Integration Testing** - Validate all components work together

## Testing Requirements

After implementation, verify:
1. ✅ XR-Manager appears in `xrandr --listoutputs`
2. ✅ XR-0 can be created via `xrandr --output XR-Manager --set CREATE_XR_OUTPUT "XR-0:1920:1080:60"`
3. ✅ XR-0 appears in `xrandr --listoutputs` after creation
4. ✅ AR mode toggle hides/shows physical XR connector correctly (`xrandr --output XR-Manager --set AR_MODE 1`)
5. ✅ Physical XR connector is marked as `non_desktop` when appropriate
6. 🚧 XR-0 framebuffer is accessible via DRM API (test with `breezy_xfce4_renderer`)
7. 🚧 Mode changes via XR_WIDTH, XR_HEIGHT, XR_REFRESH properties work correctly
8. 🚧 DMA-BUF export provides zero-copy framebuffer access
9. 🚧 End-to-end workflow: calibration → XR-0 creation → AR mode → renderer startup

## Related Files

- **Implementation:** `xserver/hw/xfree86/drivers/modesetting/drmmode_xr_virtual.c`
- **Design Docs:**
  - `breezy-desktop/XORG_VIRTUAL_XR_API.md`
  - `breezy-desktop/BREEZY_X11_TECHNICAL.md` (section 7.2)
  - `breezy-desktop/xfce4_ar_support_via_xorg_virtual_xr_connector.plan.md`
- **Renderer Code:** `breezy-desktop/xfce4/renderer/` (expects virtual connector to be available)
- **Backend Code:** `breezy-desktop/xfce4/src/xfce4_backend.py`
