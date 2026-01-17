# Xorg Virtual XR Connector Implementation Status

## Overview

This document tracks the implementation status of the Xorg modesetting driver (`xserver/hw/xfree86/drivers/modesetting/drmmode_xr_virtual.c`) to support virtual XR outputs for X11.

**📖 For detailed technical documentation on using virtual XR outputs, see: [`xserver/VIRTUAL_XR_OUTPUTS.md`](../../xserver/VIRTUAL_XR_OUTPUTS.md)**

That document covers:
- Complete API reference for virtual output creation and management
- DMA-BUF capture workflow and integration guide
- Keep-alive mechanism and DPMS management
- Performance comparison: DMA-BUF vs XShm
- Code examples for client applications

## Current Implementation Status

Based on code review of `drmmode_xr_virtual.c`:

### ✅ Completed

**Xorg Virtual XR Infrastructure**:
- ✅ XR-Manager control output creation and RandR integration
- ✅ Virtual XR output creation infrastructure (CREATE_XR_OUTPUT, DELETE_XR_OUTPUT)
- ✅ Virtual CRTC creation for virtual outputs (software-based CRTCs)
- ✅ Off-screen framebuffer/pixmap creation (GBM and dumb buffer support)
- ✅ AR mode logic (hide/show physical XR connector via `non_desktop` property)
- ✅ Physical XR display detection via EDID and `non_desktop` marking
- ✅ RandR integration (outputs appear in `xrandr` and Display Settings)
- ✅ **FRAMEBUFFER_ID property** on virtual outputs for zero-copy DMA-BUF capture
- ✅ **Automatic DPMS management** based on keep-alive signals (5-second inactivity threshold)
- ✅ **Keep-alive mechanism** via FRAMEBUFFER_ID property queries

**Breezy Desktop (GNOME)**:
- Breezy Desktop working on X11 under GNOME
- Mutter integration for 3D rendering
- DisplayConfig D-Bus interface support
- IMU integration via XRLinuxDriver

### 🚧 In Progress

#### 1. **Mode Handling for Virtual Outputs** (Required)

**What's needed:**
- Implement `drmmode_xr_virtual_crtc_set_mode_major` to handle standard RandR mode changes
- When mode changes via `RRSetCrtcConfig` or standard RandR APIs, update the virtual output's framebuffer size
- Ensure mode lists are properly reported for virtual outputs (fixed set of common resolutions: 1920x1080, 2560x1440, 3840x2160, etc.)
- Resize off-screen framebuffers when mode changes (destroy old, create new at new resolution)

**Why it's needed:**
- Allows `breezy-desktop` to dynamically control the resolution of the virtual desktop using standard RandR protocols
- Users can resize virtual displays using standard X11 display tools (xrandr, display settings GUIs)
- Essential for adapting to different AR content and user preferences

**Current status:** `drmmode_xr_virtual_set_modes` creates modes, but `set_mode_major` callback needs implementation to handle framebuffer resizing

**Reference:** Standard RandR mode-setting APIs (`RRSetCrtcConfig`, CRTC's `set_mode_major` callback)

#### 2. **DRM Framebuffer Export for Zero-Copy Capture** (✅ Complete)

**What's implemented:**
- ✅ **FRAMEBUFFER_ID RandR property** on virtual outputs exposes framebuffer ID to renderer
- ✅ Property automatically updates when framebuffer changes (mode switch)
- ✅ **Keep-alive mechanism**: Querying FRAMEBUFFER_ID property signals active consumption
- ✅ **Automatic DPMS management**: Output transitions to Standby after 5 seconds of inactivity
- ✅ Keep-alive resets inactivity timer and enables DPMS when output was inactive
- ✅ CRTC destruction properly cleans up resources (prevents crashes during deletion)

**Renderer Integration:**
- ✅ `breezy-desktop` renderer implements keep-alive in separate thread (`drm_capture_keep_alive()`)
- ✅ `x11-streamer` implements keep-alive in separate thread (`x11_context_keep_alive_output()`)
- ✅ Both use cached X11 connections to avoid blocking capture loops

**Technical Details:**
- See [`xserver/VIRTUAL_XR_OUTPUTS.md`](../../xserver/VIRTUAL_XR_OUTPUTS.md) for complete DMA-BUF capture workflow
- Query `FRAMEBUFFER_ID` property via XRandR extension (`XRRGetOutputProperty()`)
- Use framebuffer ID with `drmModeGetFB()` → `drmPrimeHandleToFD()` for zero-copy capture
- Query property every 1-2 seconds in separate thread to signal keep-alive

**Reference:** `x11/renderer/DMA_BUF_OPTIMIZATION.md`, `KMS_CONNECTOR_AND_FB_ID_OPTIONS.md`

#### 3. **X11 Backend Integration** (Required)

**What's needed:**
- Calibration detection and monitoring from XRLinuxDriver
- XR-0 creation via XR-Manager API after calibration completes
- AR mode enablement after XR-0 creation
- Renderer process management (start/stop)
- Integration with Breezy Desktop UI

**Why it's needed:**
- The X11 backend (`x11/src/x11_backend.py`) needs to orchestrate the full workflow
- Must coordinate between XRLinuxDriver, Xorg virtual outputs, and the 3D renderer

**Reference:** `x11/src/x11_backend.py` - Backend implementation

#### 4. **Integration Testing** (Required)

**What's needed:**
- End-to-end testing of the complete workflow
- Validation that all components work together correctly
- Performance testing and optimization

## Implementation Order (Recommended)

1. **Mode Handling** - Required for dynamic resolution changes
2. **DRM Framebuffer Export** - Required for zero-copy capture (DMA-BUF)
3. **X11 Backend Integration** - Required for end-to-end workflow
4. **Integration Testing** - Validate all components work together

## Testing Requirements

After implementation, verify:
1. ✅ XR-Manager appears in `xrandr --listoutputs`
2. ✅ XR-0 can be created via `CREATE_XR_OUTPUT` property on XR-Manager output
3. ✅ XR-0 appears in `xrandr --listoutputs` after creation
4. ✅ AR mode toggle hides/shows physical XR connector correctly (set `non-desktop` property on physical XR output)
5. ✅ Physical XR connector is marked as `non_desktop` when appropriate
6. ✅ XR-0 `FRAMEBUFFER_ID` property is accessible via RandR
7. ✅ Keep-alive mechanism works (querying `FRAMEBUFFER_ID` prevents DPMS standby)
8. ✅ DPMS automatically transitions to Standby after 5 seconds of inactivity
9. ✅ DMA-BUF export provides zero-copy framebuffer access (tested in `breezy_x11_renderer` and `x11-streamer`)
10. 🚧 Mode changes via standard RandR APIs (xrandr --output XR-0 --mode) work correctly
11. 🚧 End-to-end workflow: calibration → XR-0 creation → AR mode → renderer startup

## Related Files

- **Implementation:** `xserver/hw/xfree86/drivers/modesetting/drmmode_xr_virtual.c`
- **Technical Documentation:**
  - **[`xserver/VIRTUAL_XR_OUTPUTS.md`](../../xserver/VIRTUAL_XR_OUTPUTS.md)** - **Complete API reference, architecture, and integration guide** (read this first!)
  - `breezy-desktop/BREEZY_X11_TECHNICAL.md` (section 7.2)
- **Renderer Code:** `breezy-desktop/x11/renderer/` (expects virtual connector to be available)
- **Backend Code:** `breezy-desktop/x11/src/x11_backend.py`
