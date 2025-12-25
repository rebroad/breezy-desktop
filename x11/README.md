# Breezy Desktop X11 Backend

This directory contains the X11 backend for Breezy Desktop, enabling virtual display creation and management on X11 desktop environments.

## Overview

Unlike GNOME (which uses Mutter's overlay approach) and KDE (which uses KWin's compositor APIs), X11 doesn't have built-in compositor APIs for creating virtual displays. This backend uses **virtual XR outputs** in the Xorg modesetting driver to enable efficient virtual display creation.

## Architecture

**Virtual XR Outputs Approach (Current/Planned):**

1. **Virtual XR outputs** (XR-0, XR-1, etc.) are created via the Xorg modesetting driver with virtual CRTCs
2. **Physical XR display** is hidden from Display Settings (detected via EDID, marked as `non_desktop`)
3. **Desktop compositor** renders to virtual outputs via virtual CRTCs (single render pass)
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
- X11 backend implementation
- 3D renderer for X11 (reading from virtual outputs, applying transformations, rendering to physical XR display)

See [`../BREEZY_X11_TECHNICAL.md`](../BREEZY_X11_TECHNICAL.md) for detailed technical documentation.

## Prerequisites

- X11 session running on **X11**
- **Xorg with virtual XR output support** (modesetting driver with virtual CRTCs)
- Tools:
  - `xrandr` – X11 display management
  - `python3` – Python runtime

**Note:** The old dummy driver approach is no longer recommended. Virtual XR outputs in the modesetting driver provide better integration and functionality.

## X11 Backend Setup

From your `breezy-desktop` source tree:

```bash
cd breezy-desktop/x11
chmod +x bin/setup
./bin/setup
```

Or with a version number:

```bash
./bin/setup v1.0.0
```

This will:

- Check for prerequisites (xrandr, python3, XR-Manager output)
- Install the XR driver

**Prerequisites:**
- X11/Xorg session (not Xwayland)
- Xorg with modesetting driver that includes virtual XR output support (XR-Manager)
- Python 3
- `xrandr` command-line tool

**Note:** This setup script installs only the X11 backend and XR driver. The Breezy Desktop UI application requires GTK4 and libadwaita if you want to use it.

## Usage (Once Implementation is Complete)

- Launch Breezy Desktop from the applications menu while running X11 on X11.
- Breezy will:
  - Create virtual XR outputs (XR-0, XR-1, etc.) via RandR
  - Hide the physical XR display from Display Settings
  - Configure desktop compositor to render to virtual outputs
  - Capture from virtual outputs, apply 3D transformations, and render to physical XR display
- Virtual outputs will appear in Display Settings like normal monitors, allowing you to configure them as needed.

## Architecture

- `src/x11_backend.py` - Main backend implementation (uses XR-Manager API)
- `bin/setup` - Setup/installation script

## Implementation Details

For detailed technical information about the virtual XR outputs implementation, see:
- [`../BREEZY_X11_TECHNICAL.md`](../BREEZY_X11_TECHNICAL.md) - Complete technical documentation
- [`../XORG_VIRTUAL_XR_API.md`](../XORG_VIRTUAL_XR_API.md) - Xorg virtual XR connector API design

## Future Improvements

Potential improvements for better X11 support:

1. **Wayland Support:** If X11 gains Wayland support, we could use PipeWire virtual displays
2. **Multiple Virtual Displays:** Support for XR-0, XR-1, etc. for multi-monitor setups
3. **Dynamic Resolution Switching:** Allow changing virtual output resolution on the fly

