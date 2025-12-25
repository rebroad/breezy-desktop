# What is a KMS Connector?

## Overview

**KMS** stands for **Kernel Mode Setting** - it's the part of the Linux DRM (Direct Rendering Manager) subsystem that manages display hardware in the kernel.

A **KMS connector** is a kernel-side DRM object that represents a physical display connector on the graphics card (e.g., HDMI port, DisplayPort, VGA, etc.).

## Physical KMS Connectors

When you plug in a monitor to your graphics card:

1. The kernel/DRM detects the hardware connector (e.g., "HDMI-A-1")
2. Creates a KMS connector object in the kernel
3. The connector appears in `drmModeGetResources()` when queried from userspace
4. Xorg's modesetting driver enumerates these connectors via `drmModeGetConnector()`
5. Creates corresponding `xf86Output` instances for each connector

In code, this looks like:
```c
drmModeResPtr mode_res = drmModeGetResources(fd);
for (int i = 0; i < mode_res->count_connectors; i++) {
    drmModeConnectorPtr connector = drmModeGetConnector(fd, mode_res->connectors[i]);
    // This connector came from the kernel - it's a "KMS connector"
    // Xorg creates an xf86Output for it
}
```

The key point: **Physical connectors come from the kernel** - they represent real hardware.

## Virtual XR Outputs (Current Implementation - Option B)

Our virtual XR outputs (XR-Manager, XR-0, XR-1, etc.) are **NOT KMS connectors**:

- They have `drmmode_output->mode_output = NULL` (no `drmModeConnectorPtr`)
- They are created purely in userspace via `xf86OutputCreate()`
- They exist only in Xorg's RandR layer
- They don't appear in `drmModeGetResources()` from the kernel
- They don't show up in DRM tools like `modetest` or `drm_info`

This is **Option B** from the design document - "userspace-only" virtual outputs.

## What Would a KMS Virtual Connector Be (Option A)?

A **KMS virtual connector** would be a connector created **in the kernel/DRM layer** that:

- Appears in `drmModeGetResources()` just like physical connectors
- Has a real `drmModeConnectorPtr` that can be queried via KMS APIs
- Would be visible to DRM tools like `modetest`, `drm_info`, `igt`
- Would integrate naturally with DRM atomic modesetting APIs
- Would require kernel/DRM driver changes (e.g., a DRM driver that supports virtual connectors)

**Example**: The `virtio-gpu` driver in the kernel can create virtual connectors for virtual machines. These appear in `drmModeGetResources()` even though they're not physical hardware.

## Why We Use Option B (Current)

- ✅ **No kernel changes required** - userspace-only implementation
- ✅ **Faster to implement** - can iterate quickly
- ✅ **Sufficient for our needs** - RandR integration works perfectly
- ✅ **Can upgrade later** - Option A could be added if needed

## Why Option A Might Be Useful (Future)

- 🔧 **Better DRM tooling** - would show up in `modetest`, `drm_info`, etc.
- 🔧 **More consistent** - looks exactly like a physical display to all DRM code
- 🔧 **Atomic modesetting** - could use DRM atomic APIs more directly
- 🔧 **Debugging** - easier to inspect with standard DRM tools
- 🔧 **Kernel visibility** - appears in kernel's view of the system

## Summary

- **KMS connector** = kernel-side DRM object representing a display connector (physical or virtual)
- **Our virtual outputs** = userspace-only RandR outputs (no KMS connector)
- **KMS virtual connector** = kernel-side virtual connector (not implemented, would require kernel changes)

