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

## Prerequisites

- XFCE4 session running on **X11**.
- Xorg dummy video driver:
  - Debian/Ubuntu: `xserver-xorg-video-dummy`
  - Arch Linux: `xf86-video-dummy`
  - Fedora: `xorg-x11-drv-dummy`
- Tools:
  - `xrandr` – X11 display management
  - `cvt` – Modeline generation
  - `python3` – Python runtime

Example package installation (Debian/Ubuntu):

```bash
sudo apt install xrandr xserver-xorg-video-dummy
```

## Xorg Configuration (Real GPU + Dummy Screen)

To give Breezy a dedicated virtual desktop while keeping your normal XFCE desktop intact, configure Xorg with:

- **Screen 0**: your real GPU (e.g. `amdgpu`) driving your laptop panel and external monitor.
- **Screen 1**: a dummy screen (e.g. `DUMMY0`) that acts as the virtual display source.

An example `/etc/X11/xorg.conf` (adjust `BusID` to match your GPU from `lspci`):

```text
Section "Device"
    Identifier  "AMDGPU"
    Driver      "amdgpu"
    BusID       "PCI:4:0:0"
EndSection

Section "Monitor"
    Identifier  "LaptopPanel"
EndSection

Section "Screen"
    Identifier  "Screen0"
    Device      "AMDGPU"
    Monitor     "LaptopPanel"
    DefaultDepth 24
EndSection


Section "Device"
    Identifier  "BreezyDummyDevice"
    Driver      "dummy"
    VideoRam    262144
EndSection

Section "Monitor"
    Identifier  "BreezyVirtualMonitor"
    HorizSync   5.0 - 100.0
    VertRefresh 5.0 - 60.0
EndSection

Section "Screen"
    Identifier  "Screen1"
    Device      "BreezyDummyDevice"
    Monitor     "BreezyVirtualMonitor"
    DefaultDepth 24
    SubSection "Display"
        Depth   24
        Virtual 3840 2160
    EndSubSection
EndSection


Section "ServerLayout"
    Identifier  "MultiScreen"
    Screen      0 "Screen0" 0 0
    Screen      1 "Screen1" RightOf "Screen0"
EndSection
```

After creating or editing `/etc/X11/xorg.conf`, restart your display manager (or reboot).
You should then see, from your XFCE session:

```bash
xrandr            # shows eDP / DisplayPort-0 on Screen 0
xrandr --screen 1 # shows DUMMY0 on Screen 1
```

Because a visible dummy screen can “steal” the mouse cursor, it is recommended to keep `DUMMY0` **off by default** and only enable it when Breezy needs it.

One way to do that is an Xsession hook such as:

```bash
sudo tee /etc/X11/Xsession.d/90-disable-breezy-dummy >/dev/null << 'EOF'
#!/bin/sh
command -v xrandr >/dev/null 2>&1 || exit 0
xrandr --screen 1 >/dev/null 2>&1 || exit 0
xrandr --screen 1 --output DUMMY0 --off >/dev/null 2>&1 || true
EOF
sudo chmod +x /etc/X11/Xsession.d/90-disable-breezy-dummy
```

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

## Usage

- Launch Breezy Desktop from the applications menu while running XFCE4 on X11.
- When the XFCE4 backend needs a virtual display, it will:
  - Enable `DUMMY0` on **Screen 1**.
  - Create a high‑resolution mode (e.g. 3840×2160) via `xrandr --screen 1`.
  - Use that screen as the source for the 3D renderer.
- When Breezy is done with the virtual display, it should turn `DUMMY0` back off again:

```bash
xrandr --screen 1 --output DUMMY0 --off
```

## Architecture

- `src/xfce4_backend.py` - Main backend implementation
- `src/virtualdisplay_xfce4.py` - Virtual display creation script
- `bin/breezy_xfce4_setup` - Installation script

## Future Improvements

Potential improvements for better XFCE4 support:

1. **Wayland Support:** If XFCE4 gains Wayland support, we could use PipeWire virtual displays
2. **DRM Lease:** Use DRM lease API for better virtual display support
3. **Custom Compositor:** Integrate with XFCE4's compositor (if available) for 3D rendering

