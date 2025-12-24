# Breezy Desktop XFCE4 Backend

This directory contains the XFCE4 backend for Breezy Desktop, enabling virtual display creation and management on XFCE4 desktop environments.

## Overview

Unlike GNOME (which uses Mutter's overlay approach) and KDE (which uses KWin's compositor APIs), XFCE4 doesn't have built-in compositor APIs for creating virtual displays. This backend uses **virtual XR outputs** in the Xorg modesetting driver to enable efficient virtual display creation.

## Architecture

**Virtual XR Outputs Approach (Current/Planned):**

1. **Virtual XR outputs** (XR-0, XR-1, etc.) are created via the Xorg modesetting driver with virtual CRTCs
2. **Physical XR display** is hidden from Display Settings (detected via EDID, marked as `non_desktop`)
3. **XFCE compositor** renders to virtual outputs via virtual CRTCs (single render pass)
4. **Breezy's 3D renderer** captures from virtual outputs, applies 3D transformations, and renders to the physical XR display

**Why Virtual Outputs (Not Dummy Driver):**

The old dummy driver approach (`xf86-video-dummy`) had significant limitations:
- **Separate X screens**: Dummy driver creates Screen 1, which is isolated from Screen 0
- **No window dragging**: Cannot drag windows between Screen 0 and Screen 1
- **Cursor issues**: Mouse cursor can get "stuck" on dummy screen
- **Configuration complexity**: Requires custom Xorg configuration with multiple screens

Virtual XR outputs solve these issues:
- **Single X screen**: All outputs on the same screen, windows can be moved freely
- **RandR integration**: Virtual outputs appear in Display Settings like normal monitors
- **Better cursor handling**: System cursor can be hidden on virtual outputs
- **Resolution flexibility**: Virtual outputs can be any resolution

## Current Status

**🚧 In Progress:**
- Virtual CRTC creation for virtual outputs (prerequisite)
- Physical XR display detection and hiding via EDID (prerequisite)
- XFCE4 backend implementation
- 3D renderer for XFCE4 (reading from virtual outputs, applying transformations, rendering to physical XR display)

See [`../BREEZY_X11_TECHNICAL.md`](../BREEZY_X11_TECHNICAL.md) for detailed technical documentation.

## Prerequisites

- XFCE4 session running on **X11**
- **Xorg with virtual XR output support** (modesetting driver with virtual CRTCs)
- Tools:
  - `xrandr` – X11 display management
  - `python3` – Python runtime

**Note:** The old dummy driver approach is no longer recommended. Virtual XR outputs in the modesetting driver provide better integration and functionality.

## XFCE4 Backend Setup

From your `breezy-desktop` source tree:

```bash
cd breezy-desktop
chmod +x xfce4/bin/breezy_xfce4_setup
./xfce4/bin/breezy_xfce4_setup
```

This will:

- Install a `virtualdisplay_xfce4` launcher into `~/.local/bin`.
- Register the `breezy-desktop` source tree with your user `site-packages` via a `.pth` file, so the XFCE4 backend can be imported.
- Verify basic dependencies (`xrandr`, `cvt`, `python3`) are available.

## Usage (Once Implementation is Complete)

- Launch Breezy Desktop from the applications menu while running XFCE4 on X11.
- Breezy will:
  - Create virtual XR outputs (XR-0, XR-1, etc.) via RandR
  - Hide the physical XR display from Display Settings
  - Configure XFCE compositor to render to virtual outputs
  - Capture from virtual outputs, apply 3D transformations, and render to physical XR display
- Virtual outputs will appear in Display Settings like normal monitors, allowing you to configure them as needed.

## Architecture

- `src/xfce4_backend.py` - Main backend implementation
- `src/virtualdisplay_xfce4.py` - Virtual display creation script
- `bin/breezy_xfce4_setup` - Installation script

## Implementation Details

For detailed technical information about the virtual XR outputs implementation, see:
- [`../BREEZY_X11_TECHNICAL.md`](../BREEZY_X11_TECHNICAL.md) - Complete technical documentation
- [`../XORG_VIRTUAL_XR_API.md`](../XORG_VIRTUAL_XR_API.md) - Xorg virtual XR connector API design

## Future Improvements

Potential improvements for better XFCE4 support:

1. **Wayland Support:** If XFCE4 gains Wayland support, we could use PipeWire virtual displays
2. **Multiple Virtual Displays:** Support for XR-0, XR-1, etc. for multi-monitor setups
3. **Dynamic Resolution Switching:** Allow changing virtual output resolution on the fly

