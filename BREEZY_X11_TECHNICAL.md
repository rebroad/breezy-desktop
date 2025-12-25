# Breezy Desktop X11 Technical Documentation

## Overview

This document explains how Breezy Desktop functions on GNOME X11, including RandR interaction, 3D rendering pipeline, display presentation, and cursor handling.

## Table of Contents

1. [Display Presentation](#display-presentation)
2. [RandR Configuration](#randr-configuration)
3. [3D Rendering Pipeline](#3d-rendering-pipeline)
4. [Cursor Handling and Duplication Issue](#cursor-handling-and-duplication-issue)
5. [IMU Integration and Head Tracking](#imu-integration-and-head-tracking)
6. [Architecture Comparison: Wayland vs X11](#architecture-comparison-wayland-vs-x11)
7. [Roadmap: Virtual XR Outputs in Xorg](#roadmap-virtual-xr-outputs-in-xorg)

---

## Display Presentation

### How XR Glasses Appear to the Operating System

**XR glasses (XREAL, VITURE, etc.) are presented to the OS as a SINGLE display**, not as separate left/right eye displays.

#### Physical Display Characteristics

- **Single Display Output**: The glasses appear as one standard display device (e.g., `DisplayPort-0`, `HDMI-0`) in the XRandR output list
- **Resolution**: Typically 1920x1080 or 3840x2160, depending on the glasses model
- **Display Mode**: The glasses operate in "mirror mode" where the same image is sent to both eyes, or in "SBS (Side-by-Side)" mode where the left half of the display goes to the left eye and the right half to the right eye

#### Display Detection

The GNOME extension identifies XR glasses by their product name/vendor string:

```javascript
// From monitormanager.js
export const SUPPORTED_MONITOR_PRODUCTS = [
    'VITURE',
    'nreal air',
    'Air',
    'Air 2',
    'Air 2 Pro',
    'Air 2 Ultra',
    'One',
    'One Pro',
    'XREAL One',
    'XREAL One Pro',
    'SmartGlasses', // TCL/RayNeo
    'Rokid Max',
    'Rokid Max 2',
    'Rokid Air',
    // ...
];
```

The extension queries Mutter's DisplayConfig D-Bus interface to enumerate monitors and match against these product names.

#### SBS (Side-by-Side) Mode

When widescreen/SBS mode is enabled:
- The display is treated as a single wide display (e.g., 3840x1080)
- The left half (0-1919 pixels) is rendered to the left eye
- The right half (1920-3839 pixels) is rendered to the right eye
- The XR driver handles the physical splitting and lens-specific transformations

**Key Point**: The OS never sees "left eye" and "right eye" as separate displays. The XR driver (`xrDriver`) handles the stereoscopic rendering internally.

---

## RandR Configuration

### RandR Usage on GNOME X11

**Important**: On GNOME X11, Breezy Desktop does **NOT** directly manipulate RandR settings. Instead, it relies on Mutter's DisplayConfig D-Bus interface, which abstracts RandR operations.

#### How Mutter Handles Displays on X11

1. **Mutter as RandR Client**: Mutter acts as a RandR client and manages display configuration through the XRandR extension
2. **DisplayConfig D-Bus Interface**: Mutter exposes a D-Bus interface (`org.gnome.Mutter.DisplayConfig`) that provides:
   - Monitor enumeration
   - Mode configuration
   - Logical monitor management
   - Output properties (vendor, product, serial, etc.)

#### Breezy's RandR Interaction

The GNOME extension uses Mutter's DisplayConfig interface rather than direct RandR calls:

```javascript
// From monitormanager.js
function getMonitorConfig(displayConfigProxy, callback) {
    displayConfigProxy.GetCurrentStateRemote((result, error) => {
        // result contains: [serial, monitors, logicalMonitors, properties]
        // monitors: array of [details, modes, properties]
        // details: [connector, vendor, product, serial]
        // ...
    });
}
```

#### Optimal Mode Configuration

When a supported XR monitor is detected, Breezy can request optimal mode configuration:

```javascript
// From monitormanager.js - performOptimalModeCheck()
displayConfigProxy.ApplyMonitorsConfigRemote(
    serial,
    1, // temporary config
    updatedLogicalMonitors,
    {}, // properties
    callback
);
```

This allows Breezy to:
- Set the glasses as primary display (optional)
- Configure optimal resolution/refresh rate
- Disable physical displays when virtual displays are active (optional)

**Note**: On X11, virtual displays cannot be created (Mutter limitation), so this feature is disabled. The extension works with the physical glasses display directly.

---

## 3D Rendering Pipeline

### Overview

Breezy Desktop renders desktop content in 3D space, accounting for:
- Head rotation (from IMU data)
- Display distance
- Field of view (FOV) calculations
- Monitor wrapping schemes (horizontal/vertical)
- Curved display simulation

### Rendering Architecture

```
┌────────────────────────────────────────────────────────┐
│                    GNOME Shell                         │
│  ┌──────────────────────────────────────────────────┐  │
│  │  Breezy Desktop Extension                        │  │
│  │  ┌────────────────────────────────────────────┐  │  │
│  │  │  VirtualDisplaysActor                      │  │  │
│  │  │  - Manages virtual monitor actors          │  │  │
│  │  │  - Handles monitor placement/focus         │  │  │
│  │  └────────────────────────────────────────────┘  │  │
│  │  ┌────────────────────────────────────────────┐  │  │
│  │  │  VirtualDisplayEffect (GLSL Shader)        │  │  │
│  │  │  - 3D transformation                       │  │  │
│  │  │  - IMU-based rotation                      │  │  │
│  │  │  - FOV calculations                        │  │  │
│  │  └────────────────────────────────────────────┘  │  │
│  │  ┌────────────────────────────────────────────┐  │  │
│  │  │  CursorManager                             │  │  │
│  │  │  - Clones system cursor                    │  │  │
│  │  │  - Positions in 3D space                   │  │  │
│  │  └────────────────────────────────────────────┘  │  │
│  └──────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────┘
                        │
                        │ Composits overlay on top of normal desktop rendering
                        │ Overlay positioned at target monitor location
                        │ (XR glasses physical display)
                        ▼
┌─────────────────────────────────────────────────────────┐
│         Mutter Stage Compositor                         │
│  - Normal desktop rendering to XR monitor               │
│  - Overlay (black background + 3D-transformed clone)    │
│    covers the XR monitor region                         │
│  - System cursor rendered by X server (above overlay)   │
└─────────────────────────────────────────────────────────┘
                        │
                        ▼
┌─────────────────────────────────────────────────────────┐
│              XR Driver (xrDriver)                       │
│  - Receives composited frames                           │
│  - Applies lens-specific transformations                │
│  - Handles SBS mode splitting                           │
│  - Outputs to physical glasses                          │
└─────────────────────────────────────────────────────────┘
```

### 3D Transformation Process

#### 1. Monitor Detection and Setup

The extension identifies the XR glasses monitor:

```javascript
// From extension.js - _find_supported_monitor()
let target_monitor = this._monitor_manager.getMonitorPropertiesList()?.find(
    monitor => monitor && (SUPPORTED_MONITOR_PRODUCTS.includes(monitor.product) ||
               this.settings.get_string('custom-monitor-product') === monitor.product));
```

#### 2. Virtual Display Overlay Creation

An overlay is created that matches the physical monitor's dimensions and is positioned at the monitor's location on Mutter's stage:

```javascript
// From extension.js - _effect_enable()
this._virtual_displays_overlay = new St.Bin({
    style: 'background-color: rgba(0, 0, 0, 1);',
    clip_to_allocation: true
});
this._virtual_displays_overlay.set_position(targetMonitor.x, targetMonitor.y);
this._virtual_displays_overlay.set_size(targetMonitor.width, targetMonitor.height);
global.stage.add_child(this._virtual_displays_overlay);
```

**Important**: This overlay covers the normal desktop rendering at the XR monitor location. Mutter still renders the desktop normally to the XR monitor, but the overlay (with black background and 3D-transformed content) sits on top of it, covering the original content. The system cursor remains visible because it's rendered at the X server level (not by Mutter), so it appears above the overlay. This overlay approach is specific to Mutter/GNOME and is not available in X11, which is why X11 requires a different strategy (capturing the desktop framebuffer and rendering directly to the XR display).

#### 3. 3D Vertex Mesh Generation

The shader creates a 3D mesh of vertices representing the display surface:

```javascript
// From virtualdisplayeffect.js - createVertexMesh()
function createVertexMesh(fovDetails, monitorDetails, positionVectorNWU) {
    // Creates vertices in NWU (North-West-Up) coordinate system
    // Accounts for:
    // - Display distance
    // - Curved display simulation
    // - Monitor wrapping schemes
    // Returns array of Cogl.VertexP3T2 (position + texture coordinates)
}
```

**Coordinate System**: NWU (North-West-Up)
- **North (X)**: Forward from the user
- **West (Y)**: Left from the user's perspective
- **Up (Z)**: Vertical

#### 4. IMU-Based Rotation

The shader applies quaternion rotation from IMU data:

```javascript
// From virtualdisplayeffect.js
// IMU quaternion is applied to transform the display orientation
// based on head movement
const rotation_radians = this.monitor_placements[this.monitor_index].rotationAngleRadians;
this.set_uniform_float(this.get_uniform_location("u_rotation_x_radians"), 1, [rotation_radians.x]);
this.set_uniform_float(this.get_uniform_location("u_rotation_y_radians"), 1, [rotation_radians.y]);
```

#### 5. FOV (Field of View) Calculations

The rendering accounts for the glasses' FOV:

```javascript
// From virtualdisplaysactor.js
// Calculates how much of the virtual display is visible
// based on head orientation and FOV limits
function findFocusedMonitor(quaternion, monitorVectors, ...) {
    const lookVector = [1.0, 0.0, 0.0]; // Forward vector
    const rotatedLookVector = applyQuaternionToVector(lookVector, quaternion);
    // Calculate which monitor is in view based on FOV
}
```

#### 6. Rendering to Physical Display

The transformed 3D scene is rendered to the physical XR glasses display:

- The overlay is composited by Mutter onto the target monitor
- Mutter's compositor handles the final rendering
- The XR driver receives the rendered frames and applies:
  - Lens distortion correction
  - SBS mode splitting (if enabled)
  - Physical display output

### How Rotation/Angle is Factored In

1. **IMU Data Collection**: The XR driver continuously reads IMU (Inertial Measurement Unit) data from the glasses, providing quaternion rotation values representing head orientation.

2. **Data Stream**: IMU data is shared via shared memory (`/dev/shm/breezy_desktop_imu`):

```javascript
// From devicedatastream.js
// Reads IMU orientation quaternions from shared memory
// Updates at high frequency (typically matching display refresh rate)
```

3. **Quaternion Application**: The shader applies the quaternion rotation to transform the display plane:

```javascript
// From math.js
function applyQuaternionToVector(vector, quaternion) {
    // Rotates a 3D vector by a quaternion
    // Used to transform the display orientation based on head movement
}
```

4. **Display Position Update**: The display position in 3D space is updated based on:
   - Base position (display distance setting)
   - IMU rotation
   - Follow mode (smooth or legacy)
   - Focus state (zoomed in/out)

5. **Vertex Transformation**: The GLSL shader transforms vertices using:
   - Rotation matrices derived from IMU quaternions
   - Perspective projection based on FOV
   - Distance-based scaling

---

## Cursor Handling and Duplication Issue

### Current Implementation

Breezy Desktop uses a **cursor cloning** approach:

1. **System Cursor Hiding**: The extension hides the system cursor on the target monitor
2. **Cursor Cloning**: A cloned cursor sprite is created and positioned in 3D space
3. **Position Tracking**: Pointer position is tracked and the cloned cursor is updated

### Implementation Details

```javascript
// From cursormanager.js
_enableCloningMouse() {
    // Hide system cursor
    this._cursorTracker.set_pointer_visible(false);

    // Create cloned cursor sprite
    this._mouseSprite = new Clutter.Actor({ request_mode: Clutter.RequestMode.CONTENT_SIZE });
    this._mouseSprite.content = new MouseSpriteContent();

    // Add to cursor root actor
    this._cursorRoot = new Clutter.Actor();
    this._cursorRoot.add_child(this._mouseSprite);
}

_startCloningMouse() {
    // Update cursor sprite texture from system cursor
    this._updateMouseSprite();

    // Watch for pointer movement
    this._cursorWatch = PointerWatcher.getPointerWatcher().addWatch(
        interval,
        this._updateMousePosition.bind(this)
    );
}
```

### The Duplication Problem on X11

**Issue**: On X11, you see **two cursors**:
1. The **3D rendered cursor** (from Breezy's cursor cloning)
2. The **original system cursor** (still visible on the physical XR display)

#### Root Cause

On X11, the cursor hiding mechanism doesn't work as effectively as on Wayland:

1. **Wayland**: Mutter has full control over cursor rendering. When `set_pointer_visible(false)` is called, the cursor is completely hidden.

2. **X11**: The cursor is managed by the X server, not Mutter. When Breezy tries to hide it:
   ```javascript
   this._cursorTracker.set_pointer_visible(false);
   ```
   This may not fully hide the cursor on the physical XR display because:
   - X11 cursor rendering happens at a lower level (X server)
   - Mutter's cursor hiding may only affect Mutter's composited output
   - The physical XR display may still receive cursor updates directly from X11

#### Why This Happens

The XR glasses are a **physical X11 output**. When content is rendered:
- Mutter composites the 3D transformed content (including the cloned cursor) onto the target monitor
- **BUT** the X server may still be rendering the system cursor directly to the physical display
- This results in both cursors being visible

#### Potential Solutions

1. **X11 Cursor Hiding**: Use X11-specific APIs to hide the cursor:
   ```c
   // Would need X11 cursor hiding at X server level
   XFixesHideCursor(display, window);
   ```

2. **Cursor Unredirect**: Ensure cursor unredirect is disabled:
   ```javascript
   // Already done in extension.js
   if (global.compositor?.disable_unredirect) {
       global.compositor.disable_unredirect();
   }
   ```

3. **X11-Specific Cursor Management**: Implement X11-specific cursor hiding that works at the RandR output level.

**Current Status**: This is a known limitation on X11. The cursor duplication is less noticeable when the 3D rendered cursor is properly positioned, but both cursors remain visible.

---

## IMU Integration and Head Tracking

### IMU Data Flow

```
XR Glasses (Hardware)
    │
    │ USB HID (IMU data)
    ▼
XRLinuxDriver (xrDriver)
    │
    │ Reads IMU quaternions
    │ Writes to shared memory
    ▼
/dev/shm/breezy_desktop_imu
    │
    │ Shared memory file
    ▼
Breezy Desktop Extension
    │
    │ Reads via DeviceDataStream
    │ Updates at display refresh rate
    ▼
VirtualDisplayEffect (GLSL Shader)
    │
    │ Applies quaternion rotation
    │ Transforms 3D display position
    ▼
Rendered Output (3D Transformed)
```

### IMU Data Format

The shared memory contains:
- **Orientation quaternions**: 16 float values (4 quaternions × 4 components)
- **Position data**: 3 float values (X, Y, Z)
- **Timestamp**: Epoch timestamp for data age calculation

### Look-Ahead Prediction

To compensate for IMU data latency, Breezy implements "look-ahead" prediction:

```javascript
// From virtualdisplayeffect.js
function lookAheadMS(imuDateMs, lookAheadCfg, override) {
    const dataAge = Date.now() - imuDateMs;
    return (override === -1 ? lookAheadCfg[0] : override) + dataAge;
}
```

This predicts where the head will be when the frame is actually displayed, reducing perceived latency.

---

## Architecture Comparison: Wayland vs X11

### Wayland (Full Functionality)

```
┌─────────────────────────────────────────┐
│         GNOME Shell (Wayland)           │
│  ┌───────────────────────────────────┐  │
│  │  Mutter Compositor                │  │
│  │  - Full compositor control        │  │
│  │  - Virtual display creation        │  │
│  │  - Cursor management              │  │
│  └───────────────────────────────────┘  │
│  ┌───────────────────────────────────┐  │
│  │  Breezy Extension                 │  │
│  │  - Creates virtual displays        │  │
│  │  - 3D rendering                   │  │
│  │  - Cursor cloning (works fully)   │  │
│  └───────────────────────────────────┘  │
└─────────────────────────────────────────┘
```

**Features Available**:
- ✅ Virtual display creation (`RecordVirtual()`)
- ✅ Full cursor hiding
- ✅ Optimal monitor configuration
- ✅ Widescreen mode support

### X11 (Limited Functionality)

```
┌─────────────────────────────────────────┐
│         X Server (X11)                   │
│  ┌───────────────────────────────────┐  │
│  │  RandR Extension                   │  │
│  │  - Display enumeration             │  │
│  │  - Mode configuration              │  │
│  └───────────────────────────────────┘  │
│  ┌───────────────────────────────────┐  │
│  │  Mutter (X11 Backend)             │  │
│  │  - Acts as RandR client           │  │
│  │  - Compositor                     │  │
│  │  - NO virtual display creation    │  │
│  └───────────────────────────────────┘  │
│  ┌───────────────────────────────────┐  │
│  │  Breezy Extension                  │  │
│  │  - Works with physical display     │  │
│  │  - 3D rendering                   │  │
│  │  - Cursor cloning (partial)        │  │
│  └───────────────────────────────────┘  │
└─────────────────────────────────────────┘
```

**Features Available**:
- ❌ Virtual display creation (not supported)
- ⚠️ Cursor hiding (partial - duplication issue)
- ✅ 3D rendering (works)
- ✅ Follow mode (works)
- ⚠️ Widescreen mode (requires legacy setup)

### Key Differences

| Feature | Wayland | X11 |
|---------|---------|-----|
| Virtual Displays | ✅ Yes (via `RecordVirtual()`) | ❌ No (Mutter limitation) |
| Cursor Hiding | ✅ Full | ⚠️ Partial (duplication) |
| RandR Control | Via Mutter D-Bus | Via Mutter D-Bus (abstracted) |
| 3D Rendering | ✅ Yes | ✅ Yes |
| IMU Integration | ✅ Yes | ✅ Yes |
| Display Detection | ✅ Yes | ✅ Yes |

---

## Summary

### How It Works on X11

1. **Display Presentation**: XR glasses appear as a single display to the OS, not separate left/right displays.

2. **RandR**: Breezy doesn't directly manipulate RandR. It uses Mutter's DisplayConfig D-Bus interface, which abstracts RandR operations.

3. **3D Rendering**:
   - Desktop content is rendered in 3D space using GLSL shaders
   - IMU data (quaternions) is used to rotate the display based on head movement
   - FOV calculations determine what's visible
   - The transformed scene is composited onto the physical XR display

4. **Cursor Duplication**:
   - Breezy clones the cursor and renders it in 3D space
   - The system cursor remains visible on X11 (X server limitation)
   - This results in two cursors being visible

5. **IMU Integration**:
   - IMU data flows from hardware → XR driver → shared memory → extension → shader
   - Look-ahead prediction compensates for latency

### Limitations on X11

- **No Virtual Displays**: Cannot create additional virtual monitors
- **Cursor Duplication**: System cursor remains visible alongside cloned cursor
- **Limited RandR Control**: Must go through Mutter's abstraction layer

### What Works Well on X11

- ✅ 3D rendering and head tracking
- ✅ Follow mode (legacy and smooth)
- ✅ Display distance/zoom controls
- ✅ Monitor wrapping schemes
- ✅ IMU-based rotation

---

## Display Distance: Size Scaling, Not True Distance

### How "Display Distance" Actually Works

**Important Finding**: The "display distance" setting **does NOT adjust true distance** (which would require different images per eye for parallax). Instead, it **scales the display size uniformly**, making it appear larger or smaller.

#### Code Analysis - GNOME Extension

Looking at how `display_distance` is applied in the GNOME extension:

```javascript
// From virtualdisplayeffect.js line 384
const noRotationVector = monitorPlacement.centerNoRotate.map(coord =>
    coord * this._current_display_distance / this.display_distance_default
);
```

**Key Points**:
1. **Uniform Scaling**: All coordinates of the position vector are scaled by the same ratio (`display_distance / display_distance_default`)
2. **Same for Both Eyes**: `monitorPlacement.centerNoRotate` is the same for both eyes (it's the monitor center, not per-eye)
3. **No Parallax**: Since the scaling factor is identical for both eyes, there's no parallax effect
4. **Size Change Only**: Changing `display_distance` makes the display appear larger (closer) or smaller (farther), but doesn't create the stereoscopic depth effect that true distance adjustment would require

#### Why This Happens

For true distance adjustment with parallax, the code would need to:
- Calculate different `display_distance` values for left vs right eye
- Use those different distances to sample different parts of the desktop texture
- Create horizontal offset (disparity) between left and right eye images

The current GNOME implementation scales the **position** of the display uniformly, which changes its apparent size in the viewport, but both eyes see the same scaled position, so there's no parallax.

#### Note on ReShade/Vulkan Implementation

The ReShade/Vulkan shader (`Sombrero.frag`) uses a different approach:

```glsl
// From Sombrero.frag line 301
float display_distance = display_north_offset - final_lens_position.x;
```

Since `final_lens_position.x` is different for left vs right eye (due to different lens vectors), the calculated `display_distance` **is different per eye**. This could theoretically create parallax in the ReShade version, but this shader is used for gaming/Vulkan applications, not the GNOME desktop extension.

#### User Experience (GNOME Extension)

When a user adjusts "display distance" in the GNOME extension:
- ✅ The display appears larger (if moved closer) or smaller (if moved farther)
- ❌ There is **no depth perception** - objects don't appear to pop out or recede
- ❌ Both eyes see the **same image** (just scaled)

This explains why the setting feels like it's adjusting size rather than distance - because that's exactly what it's doing! The UI description is misleading - it should say "Display Size" rather than "Display Distance".

---

## SBS (Side-by-Side) Mode

### What SBS Mode Does

**SBS Mode** is a **hardware display mode** on the XR glasses that changes how the display is interpreted:

#### Hardware-Level Changes

When SBS mode is enabled, the glasses switch to a different display resolution:

**Non-SBS Mode**:
- Resolution: `1920x1080` (or `1920x1080@60/72/90/120Hz`)
- Display behavior: Same image sent to both eyes (mirrored), or single display mode

**SBS Mode**:
- Resolution: `3840x1080` (double width)
- Display behavior:
  - Left half (pixels 0-1919) → Left eye
  - Right half (pixels 1920-3839) → Right eye

#### Code Implementation

The XR driver controls the hardware mode:

```c
// From XRLinuxDriver/src/devices/xreal.c
const int sbs_display_modes[] = {
    DEVICE_MCU_DISPLAY_MODE_3840x1080_60_SBS,
    DEVICE_MCU_DISPLAY_MODE_3840x1080_72_SBS,
    DEVICE_MCU_DISPLAY_MODE_3840x1080_90_SBS,
    // ...
};
```

When SBS mode is enabled, the driver switches the glasses to one of these SBS display modes.

#### Shader Handling

The shader code (`Sombrero.frag`) handles SBS mode by:

1. **Detecting which half of the screen** is being rendered:
   ```glsl
   bool right_display = texcoord.x > 0.5;
   ```

2. **Using the appropriate lens vector**:
   ```glsl
   if(right_display) {
       effective_lens_vector = lens_vector_r;  // Right eye lens
   } else {
       effective_lens_vector = lens_vector;   // Left eye lens
   }
   ```

3. **Remapping texture coordinates** to treat each half as full-screen:
   ```glsl
   texcoord.x = (texcoord.x - (right_display ? 0.5 : 0.0)) * 2;
   // Left half: 0.0-0.5 becomes 0.0-1.0
   // Right half: 0.5-1.0 becomes 0.0-1.0
   ```

4. **Applying lens-specific transformations** for proper perspective correction per eye

#### Why Enable SBS Mode?

**The primary purpose of SBS mode is perspective correction per eye**, not wider desktop workspace.

Users would enable SBS mode for:

1. **Proper Perspective Correction**:
   - Each eye uses a different lens vector (`lens_vector` vs `lens_vector_r`)
   - This accounts for the physical separation between eyes (Inter-Pupillary Distance, IPD)
   - Without SBS mode, both eyes see the same image with the same perspective, which can cause eye strain
   - With SBS mode, each eye sees the desktop from its correct viewing angle, making the display more comfortable to view

2. **Different Display Distance Per Eye**:
   - In the shader, `display_distance = display_north_offset - final_lens_position.x`
   - Since `final_lens_position.x` is different for each eye (due to different lens vectors), the calculated `display_distance` **is different per eye**
   - This creates natural parallax based on the physical lens positions
   - However, both eyes still sample from the **same desktop texture** - they just view it from slightly different angles

3. **Hardware Capability**:
   - Takes advantage of the glasses' full resolution capability (3840x1080 vs 1920x1080)
   - Some glasses support higher refresh rates in SBS mode

**Important Clarification**: SBS mode does **NOT** enable a "wider desktop" where you get more screen space. Both eyes still see the **same desktop content**, just with proper perspective correction. The double-width resolution (3840x1080) is used to send different perspective-corrected views to each eye, not to display more content.

#### Why Disable SBS Mode?

Users would disable SBS mode for:

1. **Performance**:
   - SBS mode requires rendering at double width (3840x1080)
   - More resource intensive (as noted in README: "can be significantly more resource intensive")
   - May cause performance issues on older hardware

2. **Compatibility**:
   - Some applications may not work well with the wider resolution
   - May cause display issues with certain content

3. **Simplicity**:
   - Standard 1920x1080 mode is simpler and more compatible
   - No need to worry about content being split across eyes incorrectly

#### Technical Details

- **Mode Switching**: The XR driver handles the hardware mode switch, which may take a moment
- **State Synchronization**: Breezy Desktop polls the driver state to ensure SBS mode matches the UI setting
- **Fast Switching**: There's a "fast-sbs-mode-switching" option that allows quicker transitions but may cause brief display interruptions

#### Current Limitations

- SBS mode requires glasses hardware support (not all models support it)
- The mode switch is a hardware operation that may cause a brief display interruption
- Performance impact is significant on lower-end hardware

---

## 7. X11 Support via Virtual XR Outputs in Xorg

**Current Status**: Breezy Desktop works on X11 under GNOME. X11 support requires virtual XR outputs in Xorg (implementation in progress).

### 7.1 Standalone Renderer for Xorg-Based Desktops

Since GNOME support is already working, our immediate focus is on supporting other desktop environments/WMs that use Xorg (X11, i3, Openbox, etc.). Unlike GNOME which uses Mutter (a compositor with 3D rendering capabilities), these desktops do not have built-in 3D rendering capabilities, so Breezy Desktop needs to provide its own standalone 3D renderer.

#### Architecture Differences from GNOME

Xorg-based desktops (X11, i3, Openbox, etc.) differ from GNOME in several key ways:

1. **No Built-in 3D Compositor**: These desktops use compositors that handle 2D window composition but do not have 3D rendering capabilities
2. **Different Display Management**: They use RandR directly rather than Mutter's DisplayConfig D-Bus interface
3. **Standalone 3D Renderer Requirement**: Breezy Desktop must provide its own standalone 3D renderer that:
   - Captures the desktop content from virtual XR outputs (via DRM/KMS)
   - Applies 3D transformations (IMU-based head tracking, FOV calculations, display distance)
   - Renders the transformed content to the physical XR display
   - Runs as a separate process, independent of the compositor

#### Standalone Renderer Architecture

**High-Performance C-Based Renderer**:

For maximum FPS and best user experience, the standalone renderer is implemented in **C with OpenGL**, not Python. This avoids:
- Python GIL (Global Interpreter Lock) overhead
- Python runtime overhead
- Memory allocation inefficiencies

**Why compositor-specific code cannot be reused**:
- **GNOME's `virtualdisplayeffect.js`**: JavaScript code for Mutter's Cogl API - not usable in a standalone C binary
- **KWin's QML/CurvableDisplayMesh.qml**: QML code for Qt Quick 3D - not usable in a standalone C binary
- **Solution**: The renderer uses `modules/sombrero/Sombrero.frag` directly (same shader used by ReShade/Vulkan), avoiding code duplication

**Architecture**:
1. **Capture Thread**: Reads from virtual XR connector via DRM/KMS directly (no X11 screen capture like `XShmGetImage` or `XGetImage`)
2. **Render Thread**: Applies GLSL shaders (ported from `Sombrero.frag`) and renders to AR glasses display at exact refresh rate (vsync)
3. **Lock-free Ring Buffer**: Triple-buffered frame transfer between threads for zero-copy, low-latency frame passing
4. **Direct OpenGL Rendering**: No Qt/abstractions - raw OpenGL for minimum overhead

**Workflow**:
1. Glasses connect → XRLinuxDriver detects device
2. Initial calibration period (15 seconds for XREAL devices)
3. Physical VR display marked as `non_desktop` via EDID (hidden from Display Settings)
4. Virtual XR connector (XR-0) appears in same screen location
5. Desktop compositor (XFCE, i3, etc.) renders to virtual XR connector (off-screen buffer)
6. Breezy standalone renderer captures from virtual buffer via DRM/KMS
7. Renderer applies 3D transformations (IMU head tracking) via GLSL shaders (Sombrero.frag)
8. Renderer outputs directly to physical AR glasses display

**Key Performance Optimizations**:
- Lock-free ring buffer (atomic operations, memory barriers)
- Direct DRM/KMS access (bypasses X11 overhead)
- Separate capture/render threads (capture can be slower than render, render matches refresh rate exactly)
- No intermediate buffers where possible
- Direct OpenGL context on AR glasses display

#### Standalone Renderer Implementation Tasks

**🚧 Immediate Priority**:
- [x] C-based 3D renderer skeleton
- [x] DRM/KMS capture from virtual XR connector (no X11 screen capture)
- [x] GLSL shader loading from `modules/sombrero/Sombrero.frag` (no duplication - uses original file)
- [x] IMU data reader from `/dev/shm/breezy_desktop_imu`
- [x] OpenGL context creation on AR glasses display
- [x] Virtual XR connector implementation in Xorg modesetting driver
- [x] Physical XR display `non_desktop` marking via EDID
- [ ] Backend integration with Breezy Desktop UI
- [ ] Testing and validation on Xorg-based desktops

**📋 Planned Enhancements**:
- Multiple virtual displays support (XR-1, XR-2, etc.)
- Dynamic resolution switching
- Curved display support (mesh generation in C, similar to GNOME/KWin implementations)

### 7.2 Virtual XR Outputs in Xorg (X11 Implementation)

**Note**: This section describes the virtual XR outputs implementation in the Xorg modesetting driver. This is **required for X11 support** (not optional), as X11 lacks compositor APIs like Mutter/KWin. For detailed API design and implementation status, see:
- `XORG_VIRTUAL_XR_API.md` - Detailed API design, data structures, and communication interface
- `XORG_IMPLEMENTATION_STATUS.md` - Current implementation status and remaining tasks

Virtual XR outputs provide:
- Arbitrary resolution support (not limited to physical display resolution)
- Multiple virtual displays
- Better cursor management
- Resolution independence

#### Current Limitations (When Using Physical Display Directly)

1. **Fixed Resolution**: The physical XR display has a fixed resolution (e.g., 1920x1080 or 3840x2160) that cannot be changed
2. **Cursor Duplication**: Two mouse cursors may be visible - one from the system and one from Breezy's 3D rendering
3. **No Virtual Display Support**: Cannot create virtual displays of arbitrary resolutions for AR experiences

#### Solution: Virtual XR Outputs in Xorg

**Virtual XR outputs** are implemented in the Xorg modesetting driver. This enables Breezy Desktop to:

1. **Create displays of any resolution**: Virtual outputs (XR-0, XR-1, etc.) can be created with arbitrary width, height, and refresh rate
2. **Replace the physical XR display**: Instead of rendering to the fixed-resolution physical XR glasses display, Breezy can render to virtual outputs that are then composited and sent to the physical display
3. **Fix cursor duplication**: By rendering to virtual outputs (which are software-based), the system cursor can be properly hidden, leaving only Breezy's 3D-rendered cursor visible

### Technical Implementation

#### Architecture Overview

```
┌────────────────────────────────────────────────────────┐
│             X Server (X11) with Virtual XR Outputs     │
│  ┌──────────────────────────────────────────────────┐  │
│  │  Modesetting Driver                              │  │
│  │  - Virtual XR outputs (XR-0, XR-1, ...)          │  │
│  │  - Dynamic creation/resize/delete                │  │
│  │  - CRTC assignment for desktop content           │  │
│  └──────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────┐  │
│  │  RandR Extension                                 │  │
│  │  - Exposes virtual outputs to Display Settings   │  │
│  │  - Mode configuration                            │  │
│  └──────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────┘
                        │
                        │ RandR API
                        ▼
┌──────────────────────────────────────────────────────────────┐
│         Mutter (X11 Backend)                                 │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  DisplayConfig D-Bus Interface                         │  │
│  │  - Enumerates virtual XR outputs                       │  │
│  │  - Manages logical monitors                            │  │
│  └────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  Compositor (GNOME: Mutter, X11: Breezy 3D Renderer) │  │
│  │  - Composites virtual outputs                          │  │
│  │  - Renders to physical XR display (GNOME)              │  │
│  │  - Or Breezy 3D renderer reads and transforms (X11)  │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
                        │
                        │ Composited output
                        ▼
┌────────────────────────────────────────────────────────┐
│         Breezy Desktop Extension                       │
│  ┌──────────────────────────────────────────────────┐  │
│  │  Virtual Display Management                      │  │
│  │  - Creates virtual XR outputs via RandR          │  │
│  │  - Configures resolution dynamically             │  │
│  │  - 3D rendering to virtual outputs               │  │
│  └──────────────────────────────────────────────────┘  │
│  ┌──────────────────────────────────────────────────┐  │
│  │  Cursor Management                               │  │
│  │  - Hides system cursor on virtual outputs        │  │
│  │  - Renders 3D cursor only                        │  │
│  └──────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────┘
```

#### Implementation Details

**1. Virtual Output Creation**

Virtual XR outputs are created in the modesetting driver (`drmmode_xr_virtual.c`) via the XR-Manager control output:

- **XR-Manager**: Control output that manages virtual XR outputs (always present, non-desktop, disconnected)
- **Virtual Outputs (XR-0, XR-1, etc.)**: Created dynamically via `CREATE_XR_OUTPUT` property on XR-Manager
- **AR Mode**: Toggled via `AR_MODE` property on XR-Manager (hides physical XR, shows virtual XR)

For detailed API design and data structures, see `XORG_VIRTUAL_XR_API.md`.

**2. RandR Integration**

Virtual outputs are exposed through RandR:
- They appear in `xrandr --listoutputs` as `XR-0`, `XR-1`, etc.
- They are visible in Display Settings GUI (X11, GNOME)
- They support dynamic resolution changes via RandR properties (`XR_WIDTH`, `XR_HEIGHT`, `XR_REFRESH`)

**3. CRTC Assignment and Rendering Pipeline**

**Approach**: Virtual outputs use **virtual CRTCs** (software-based) that allow the desktop compositor to render to them. The **3D renderer acts as the "driver"** for these virtual outputs:

- **Virtual CRTCs**: Software-based CRTCs are created for virtual outputs (XR-0, XR-1, etc.)
- **Desktop Compositor Rendering**: The compositor (Mutter for GNOME, or XFCE compositor) renders desktop content to virtual outputs via these virtual CRTCs
- **3D Renderer as CRTC Driver**: Breezy's 3D renderer reads the framebuffers from virtual outputs, applies 3D transformations (IMU-based rotation, FOV, etc.), and composites the result to the physical XR display
- **Physical Display Hidden**: The physical XR display (e.g., XREAL glasses) is marked as `non_desktop` via EDID detection so it doesn't appear in Display Settings. Display Settings only show virtual outputs (the 2D desktop arrangement), not the physical XR sink.

**Key Insight**: The physical XR display is treated as a "sink" that receives the final 3D-rendered output. It's not part of the 2D desktop layout, so it's hidden from Display Settings. Virtual outputs (XR-0, XR-1) are the outputs that appear in Display Settings and represent the 2D desktop arrangement that gets transformed by the 3D renderer.

**4. Resolution Flexibility**

Virtual outputs enable:
- Creating displays of any resolution (e.g., 2560x1440, 3840x2160, custom aspect ratios)
- Resizing dynamically via RandR properties
- Supporting multiple virtual displays simultaneously (XR-0, XR-1, etc.)

**5. Cursor Duplication Fix**

By rendering to virtual outputs instead of the physical XR display:
- The system cursor can be hidden on virtual outputs (X11 cursor hiding works for software outputs)
- Only Breezy's 3D-rendered cursor is visible
- The physical XR display receives the composited output (including the 3D cursor)

### Implementation Status

For detailed implementation status, completed items, and remaining tasks, see **`XORG_IMPLEMENTATION_STATUS.md`**.

### Benefits of Virtual XR Outputs

Virtual XR outputs provide:

1. **Resolution Independence**: Breezy Desktop can create virtual displays of any resolution, not limited by the physical XR glasses hardware
2. **Multiple Virtual Displays**: Support for multiple virtual outputs (XR-0, XR-1, etc.) for multi-monitor setups
3. **Cursor Fix**: Eliminates the cursor duplication issue by rendering to virtual outputs where cursor hiding works correctly
4. **Better Integration**: Virtual outputs appear in standard Display Settings tools, making them easier to configure
5. **Wayland Parity**: Brings X11 functionality closer to the Wayland implementation's virtual display capabilities

*Note: These benefits are realized in the current implementation. Unlike GNOME which uses Mutter's overlay approach, X11 requires virtual outputs to avoid double rendering.*

### X11 Implementation Path

**Architecture**: Virtual XR outputs in Xorg are a **prerequisite** for efficient X11 support. The workflow is:

1. **Virtual XR outputs are created** (XR-0, XR-1, etc.) via Xorg modesetting driver with virtual CRTCs
2. **Physical XR display is hidden** from Display Settings (detected via EDID, marked as `non_desktop` or disabled)
3. **XFCE compositor renders to virtual outputs** (XR-0, etc.) via virtual CRTCs (single render pass)
4. **Breezy 3D renderer captures from virtual outputs** and reads the framebuffers (via DRM/X11 APIs)
5. **Breezy applies 3D transformations** based on IMU data (head tracking, rotation, FOV calculations, display distance)
6. **Breezy renders transformed content directly to the physical XR display** via OpenGL

**Why virtual outputs are required**:
- **Efficiency**: Single render pass from compositor (avoids double rendering)
- **No visual artifacts**: Physical display only shows transformed content (compositor doesn't render to it)
- **Cursor handling**: System cursor can be hidden on virtual outputs (software-based), eliminating cursor duplication
- **Resolution flexibility**: Virtual outputs can be any resolution, not limited by physical display hardware
- **Performance**: More efficient GPU usage compared to capturing/transforming from physical display

**Note**: Attempting to work directly with the physical XR display would require double rendering (compositor renders to XR display, then Breezy captures and re-renders to the same display), which is inefficient, causes visual artifacts, and doesn't solve the cursor duplication issue.

**Implementation Path for X11**:

1. **Complete virtual XR output infrastructure** in Xorg modesetting driver (virtual CRTCs, RandR integration)
2. **Hide physical XR display** from Display Settings (detect via EDID, mark as `non_desktop`)
3. **XFCE backend implementation** that:
   - Creates/manages virtual XR outputs via RandR
   - Configures XFCE compositor to render to virtual outputs
4. **Breezy 3D renderer** that:
   - Reads framebuffers from virtual XR outputs (via DRM/X11 APIs)
   - Applies 3D transformations using OpenGL/GLSL shaders (port from GNOME shader)
   - Renders transformed content to physical XR display via OpenGL
   - Manages cursor (hides system cursor on virtual outputs, renders 3D cursor)
5. **IMU integration**: Reads from `/dev/shm/breezy_desktop_imu` for head tracking data

**Note on GNOME Architecture (for comparison)**: On GNOME, Mutter renders the desktop normally to the XR monitor, but then a black overlay (`St.Bin`) is positioned at the monitor's location on Mutter's stage. This overlay contains a `Clutter.Clone` of the desktop content (`Main.layoutManager.uiGroup`) with 3D transformations applied via a GLSL shader. The overlay covers most of the original content, so only the 3D-transformed version is visible. The system cursor remains visible because it's rendered at the X server level and cannot be fully hidden on X11. This overlay approach is not available in X11, which is why virtual outputs are necessary.


---

## References

- **Extension Source**: `breezy-desktop/gnome/src/`
- **XR Driver**: `XRLinuxDriver/`
- **Xorg Virtual XR API Design**: `breezy-desktop/XORG_VIRTUAL_XR_API.md`
- **Xorg Implementation Status**: `breezy-desktop/XORG_IMPLEMENTATION_STATUS.md`
- **Mutter DisplayConfig**: `breezy-desktop/gnome/src/dbus-interfaces/org.gnome.Mutter.DisplayConfig.xml`
- **Shader Code**: `breezy-desktop/modules/sombrero/Sombrero.frag`
- **Xorg Virtual XR Implementation**: `xserver/hw/xfree86/drivers/modesetting/drmmode_xr_virtual.c`

