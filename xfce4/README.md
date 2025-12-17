# Breezy Desktop XFCE4 Backend

This directory contains the XFCE4 backend for Breezy Desktop, enabling virtual display creation and management on XFCE4 desktop environments.

## Overview

Unlike GNOME (which uses Mutter's compositor APIs) and KDE (which uses KWin's compositor APIs), XFCE4 doesn't have built-in compositor APIs for creating virtual displays. This backend provides an alternative implementation using X11/xrandr methods.

## Limitations

**Important:** Virtual display creation on XFCE4/X11 has significant limitations:

1. **No True Virtual Displays:** XFCE4/X11 cannot create true virtual displays like GNOME/KDE can. The backend uses xrandr modelines, which have limited functionality.

2. **Requires xf86-video-dummy:** For best results, you need the `xf86-video-dummy` driver installed, which requires X server configuration and potentially a restart.

3. **No Compositor Integration:** Virtual displays won't be rendered in 3D space by the compositor. They'll appear as regular X11 outputs.

4. **Wayland Recommended:** For full virtual display functionality, consider using a Wayland session if available.

## Installation

Run the setup script:

```bash
./xfce4/bin/breezy_xfce4_setup
```

This will:
- Install the `virtualdisplay_xfce4` script
- Install the XFCE4 backend Python module
- Check for required dependencies

## Dependencies

- `xrandr` - X11 display management
- `cvt` - Modeline generation
- `python3` - Python runtime
- `xf86-video-dummy` (optional but recommended) - Virtual display driver

## Usage

The XFCE4 backend is automatically detected and used when running Breezy Desktop on XFCE4. No manual configuration is needed.

## Architecture

- `src/xfce4_backend.py` - Main backend implementation
- `src/virtualdisplay_xfce4.py` - Virtual display creation script
- `bin/breezy_xfce4_setup` - Installation script

## Future Improvements

Potential improvements for better XFCE4 support:

1. **Wayland Support:** If XFCE4 gains Wayland support, we could use PipeWire virtual displays
2. **DRM Lease:** Use DRM lease API for better virtual display support
3. **Custom Compositor:** Integrate with XFCE4's compositor (if available) for 3D rendering

