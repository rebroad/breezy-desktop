# Xorg Virtual XR Connector Implementation Status

## Overview

This document tracks what still needs to be implemented in the Xorg modesetting driver (`xserver/hw/xfree86/drivers/modesetting/drmmode_xr_virtual.c`) to support virtual XR outputs for XFCE4.

## Current Implementation Status

Based on code review of `drmmode_xr_virtual.c`:

### ✅ Completed
- XR-Manager output creation and RandR integration
- Basic output creation infrastructure
- RandR property handlers (CREATE_XR_OUTPUT, DELETE_XR_OUTPUT, etc.)
- Output enumeration hooks

### 🚧 Still Needed

#### 1. **Virtual CRTC Creation** (Critical - Prerequisite for XFCE4)

**What's needed:**
- Software-based virtual CRTCs that can be assigned to virtual XR outputs (XR-0, XR-1, etc.)
- Virtual CRTCs should allow the desktop compositor (XFCE compositor) to render to them
- CRTC assignment and mode setting for virtual outputs

**Why it's needed:**
- XFCE4 compositor needs CRTCs to render desktop content to virtual outputs
- Without virtual CRTCs, the compositor cannot render to XR-0
- This is different from GNOME/Mutter which uses overlay-based rendering

**Reference:** `BREEZY_X11_TECHNICAL.md` lines 910-917 describe virtual CRTC approach

#### 2. **Off-screen Framebuffer/Pixmap Creation** (Critical)

**What's needed:**
- Create off-screen framebuffers (DRM FBs or X11 pixmaps) for virtual XR outputs
- Framebuffers must be accessible via DRM API for capture by `breezy_xfce4_renderer`
- Support for DMA-BUF export of framebuffers (for zero-copy capture)

**Why it's needed:**
- The 3D renderer needs to capture frames from XR-0's framebuffer via DRM/KMS
- Current renderer implementation in `xfce4/renderer/drm_capture.c` expects DRM framebuffers

**Reference:** `xfce4/renderer/IMPLEMENTATION_STATUS.md` line 35-36

#### 3. **AR Mode Logic** (Critical)

**What's needed:**
- When AR mode is enabled: hide physical XR connector from RandR, show virtual XR connector
- When AR mode is disabled: show physical XR connector, hide virtual XR connector
- Toggle via RandR property on XR-Manager output

**Current status:** TODO comment in code (line 902-904 of `drmmode_xr_virtual.c`)

**Why it's needed:**
- Prevents Xorg from trying to render 2D content directly to physical VR headset
- Ensures desktop compositor only renders to virtual outputs in AR mode

#### 4. **Physical XR Display Detection and `non_desktop` Marking** (Critical)

**What's needed:**
- Detect physical XR displays (XREAL, VITURE, etc.) via EDID vendor/product strings
- Mark physical XR connector as `non_desktop` when AR mode is active
- Hide physical XR connector from Display Settings GUI when in AR mode

**Why it's needed:**
- Unlike VR headsets (Valve Index, HTC Vive), XREAL/VITURE glasses are NOT automatically marked as non-desktop by the kernel
- Must be done explicitly via EDID detection and RandR property setting

**Reference:** `BREEZY_X11_TECHNICAL.md` line 915

#### 5. **Mode Handling for Virtual Outputs** (Required)

**What's needed:**
- Virtual outputs should support dynamic resolution changes via RandR properties (XR_WIDTH, XR_HEIGHT, XR_REFRESH)
- Mode lists for virtual outputs (fixed set of common resolutions: 1920x1080, 2560x1440, 3840x2160, etc.)
- Mode setting callbacks that update framebuffer size when mode changes

**Reference:** `XORG_VIRTUAL_XR_API.md` lines 125-129

#### 6. **DRM Framebuffer Export** (Required for Zero-Copy)

**What's needed:**
- Export virtual output framebuffers as DMA-BUF file descriptors
- Use `drmPrimeHandleToFD` or equivalent to expose framebuffers for capture
- Ensure framebuffers are in formats compatible with EGL DMA-BUF import (typically XRGB8888 or ARGB8888)

**Why it's needed:**
- The 3D renderer uses DMA-BUF zero-copy for optimal performance
- Current renderer expects to access framebuffers via DRM API (`xfce4/renderer/drm_capture.c`)

#### 7. **Integration with Modesetting Driver** (Required)

**What's needed:**
- Hook virtual XR connector creation into modesetting driver initialization
- Ensure virtual outputs appear in `xrandr --listoutputs`
- Integrate with existing CRTC/connector enumeration code

**Reference:** `XORG_VIRTUAL_XR_API.md` lines 86-122 shows pseudo-code for initialization

## Implementation Order (Recommended)

1. **Virtual CRTC Creation** - Required for compositor rendering
2. **Off-screen Framebuffer Creation** - Required for capture
3. **AR Mode Logic** - Required to switch between modes
4. **Physical XR Display Detection** - Required to hide physical display in AR mode
5. **Mode Handling** - Required for dynamic resolution
6. **DRM Framebuffer Export** - Required for zero-copy capture
7. **Integration** - Wire everything together

## Testing Requirements

After implementation, verify:
1. XR-Manager appears in `xrandr --listoutputs`
2. XR-0 can be created via `xrandr --output XR-Manager --set CREATE_XR_OUTPUT "XR-0:1920:1080:60"`
3. XR-0 appears in `xrandr --listoutputs` after creation
4. XR-0 framebuffer is accessible via DRM API (test with `breezy_xfce4_renderer`)
5. AR mode toggle hides/shows physical XR connector correctly
6. Physical XR connector is marked as `non_desktop` when appropriate

## Related Files

- **Implementation:** `xserver/hw/xfree86/drivers/modesetting/drmmode_xr_virtual.c`
- **Design Docs:**
  - `breezy-desktop/XORG_VIRTUAL_XR_API.md`
  - `breezy-desktop/BREEZY_X11_TECHNICAL.md` (section 7.2)
  - `breezy-desktop/xfce4_ar_support_via_xorg_virtual_xr_connector.plan.md`
- **Renderer Code:** `breezy-desktop/xfce4/renderer/` (expects virtual connector to be available)

