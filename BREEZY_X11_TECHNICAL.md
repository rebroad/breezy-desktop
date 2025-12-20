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
┌─────────────────────────────────────────────────────────┐
│                    GNOME Shell                          │
│  ┌──────────────────────────────────────────────────┐  │
│  │  Breezy Desktop Extension                         │  │
│  │  ┌────────────────────────────────────────────┐  │  │
│  │  │  VirtualDisplaysActor                      │  │  │
│  │  │  - Manages virtual monitor actors          │  │  │
│  │  │  - Handles monitor placement/focus          │  │  │
│  │  └────────────────────────────────────────────┘  │  │
│  │  ┌────────────────────────────────────────────┐  │  │
│  │  │  VirtualDisplayEffect (GLSL Shader)         │  │  │
│  │  │  - 3D transformation                        │  │  │
│  │  │  - IMU-based rotation                       │  │  │
│  │  │  - FOV calculations                         │  │  │
│  │  └────────────────────────────────────────────┘  │  │
│  │  ┌────────────────────────────────────────────┐  │  │
│  │  │  CursorManager                              │  │  │
│  │  │  - Clones system cursor                     │  │  │
│  │  │  - Positions in 3D space                   │  │  │
│  │  └────────────────────────────────────────────┘  │  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                        │
                        │ Renders to target monitor
                        │ (XR glasses physical display)
                        ▼
┌─────────────────────────────────────────────────────────┐
│              XR Driver (xrDriver)                       │
│  - Receives rendered frames                             │
│  - Applies lens-specific transformations               │
│  - Handles SBS mode splitting                            │
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

An overlay is created that matches the physical monitor's dimensions:

```javascript
// From extension.js - _effect_enable()
this._virtual_displays_overlay = new St.Bin({
    style: 'background-color: rgba(0, 0, 0, 1);',
    clip_to_allocation: true
});
this._virtual_displays_overlay.set_position(targetMonitor.x, targetMonitor.y);
this._virtual_displays_overlay.set_size(targetMonitor.width, targetMonitor.height);
```

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

## Roadmap: Virtual XR Outputs in Xorg

### Current Limitations

The current X11 implementation has two major limitations:

1. **Fixed Resolution Constraint**: Breezy Desktop must work with the physical XR glasses display at its native resolution (typically 1920x1080 or 3840x2160). The extension cannot create virtual displays of arbitrary resolution, unlike the Wayland implementation which uses Mutter's `RecordVirtual()` API.

2. **Cursor Duplication**: On X11, both the system cursor (rendered by the X server) and the cloned 3D cursor (rendered by Breezy) are visible simultaneously. This occurs because:
   - The X server manages cursor rendering at a lower level
   - Mutter's cursor hiding (`set_pointer_visible(false)`) only affects Mutter's composited output
   - The physical XR display receives cursor updates directly from X11

### Solution: Virtual XR Outputs in Xorg

We are implementing **virtual XR outputs** directly in the Xorg modesetting driver. This will enable Breezy Desktop to:

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
│  │  Compositor (GNOME: Mutter, XFCE4: Breezy 3D Renderer) │  │
│  │  - Composites virtual outputs                          │  │
│  │  - Renders to physical XR display (GNOME)              │  │
│  │  - Or Breezy 3D renderer reads and transforms (XFCE4)  │  │
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

Virtual XR outputs are created in the modesetting driver (`drmmode_xr_virtual.c`):

```c
// Virtual outputs are created dynamically via RandR properties
// XR-Manager output (XR-Manager) is used for control
// Virtual outputs (XR-0, XR-1, ...) are created on demand

drmmode_xr_create_virtual_output(ScrnInfoPtr pScrn, drmmode_ptr drmmode,
                                 const char *name, int width, int height, int refresh)
```

**2. RandR Integration**

Virtual outputs are exposed through RandR:
- They appear in `xrandr --listoutputs` as `XR-0`, `XR-1`, etc.
- They are visible in Display Settings GUI (XFCE4, GNOME)
- They support dynamic resolution changes via RandR properties (`XR_WIDTH`, `XR_HEIGHT`, `XR_REFRESH`)

**3. CRTC Assignment and Rendering Pipeline**

**Approach**: Virtual outputs use **virtual CRTCs** (software-based) that allow the desktop compositor (Mutter) to render to them. The **3D renderer acts as the "driver"** for these virtual outputs:

- **Virtual CRTCs**: Software-based CRTCs are created for virtual outputs (XR-0, XR-1, etc.)
- **Desktop Compositor Rendering**: Mutter renders desktop content to virtual outputs via these virtual CRTCs
- **3D Renderer as CRTC Driver**: Breezy's 3D renderer reads the framebuffers from virtual outputs, applies 3D transformations (IMU-based rotation, FOV, etc.), and composites the result to the physical XR display
- **Physical Display Hidden**: The physical XR display (e.g., XREAL glasses) must be marked as `non_desktop` so it doesn't appear in Display Settings. Display Settings only shows virtual outputs (the 2D desktop arrangement), not the physical XR sink. **Note**: Unlike some VR headsets (Valve Index, HTC Vive, Oculus Rift), XREAL/VITURE glasses are NOT automatically marked as non-desktop by the kernel - we need to detect them via EDID vendor/product strings and mark them explicitly.

**Key Insight**: The physical XR display is treated as a "sink" that receives the final 3D-rendered output. It's not part of the 2D desktop layout, so it must be hidden from Display Settings. Virtual outputs (XR-0, XR-1) are the outputs that appear in Display Settings and represent the 2D desktop arrangement that gets transformed by the 3D renderer.

**Note on Desktop Environments**:
- **GNOME**: Uses Mutter as the compositor, which can composite virtual outputs and render to the physical XR display
- **XFCE4**: Does not have built-in 3D rendering capabilities. Breezy Desktop will need to provide its own 3D renderer that reads virtual output framebuffers, applies 3D transformations (IMU-based head tracking, FOV calculations, etc.), and renders to the physical XR display

**4. Resolution Flexibility**

Once CRTCs are assigned, virtual outputs can:
- Be created with any resolution (e.g., 2560x1440, 3840x2160, custom aspect ratios)
- Be resized dynamically via RandR properties
- Support multiple virtual displays simultaneously (XR-0, XR-1, etc.)

**5. Cursor Duplication Fix**

By rendering to virtual outputs instead of the physical XR display:
- The system cursor can be hidden on virtual outputs (X11 cursor hiding works for software outputs)
- Only Breezy's 3D-rendered cursor will be visible
- The physical XR display receives the composited output (including the 3D cursor) from Mutter

### Current Status

**✅ Completed**:
- Virtual XR output creation infrastructure in modesetting driver
- RandR integration (outputs appear in `xrandr` and Display Settings)
- Dynamic output creation/resize/delete via RandR properties
- XR-Manager control output for managing virtual outputs
- `non_desktop` property preservation in RandR

**🚧 In Progress**:
- Virtual CRTC creation for virtual outputs (software-based CRTCs that allow desktop compositor rendering)
- Physical XR display detection and hiding (detecting XREAL/VITURE glasses via EDID vendor/product strings and marking them as non-desktop to hide from Display Settings)
- 3D renderer integration (reading virtual output framebuffers and rendering to physical XR display)
- Testing with Display Settings GUI (XFCE4, GNOME)

**📋 Planned**:
- Integration with Breezy Desktop extension
- Dynamic resolution switching
- Cursor hiding on virtual outputs
- Performance optimization

### Benefits

Once complete, this implementation will provide:

1. **Resolution Independence**: Breezy Desktop can create virtual displays of any resolution, not limited by the physical XR glasses hardware
2. **Multiple Virtual Displays**: Support for multiple virtual outputs (XR-0, XR-1, etc.) for multi-monitor setups
3. **Cursor Fix**: Eliminates the cursor duplication issue by rendering to virtual outputs where cursor hiding works correctly
4. **Better Integration**: Virtual outputs appear in standard Display Settings tools, making them easier to configure
5. **Wayland Parity**: Brings X11 functionality closer to the Wayland implementation's virtual display capabilities

### Migration Path

When this feature is complete, Breezy Desktop on X11 will:

1. **Physical XR display is hidden**: The physical XR glasses display is detected via EDID vendor/product strings (XREAL, VITURE, etc.) and marked as `non_desktop`, so it doesn't appear in Display Settings. It acts as a "sink" that receives the final 3D-rendered output. **Note**: Unlike some VR headsets (Valve Index, HTC Vive, Oculus Rift), XREAL/VITURE glasses are NOT automatically marked as non-desktop by the kernel - we need to detect and mark them explicitly.

2. **Virtual outputs appear in Display Settings**: Virtual XR outputs (XR-0, XR-1, etc.) appear in Display Settings as normal displays. Users can configure their 2D desktop arrangement using these virtual outputs.

3. **Desktop compositor renders to virtual outputs**: The desktop compositor (Mutter for GNOME, or XFCE's compositor) renders desktop content to virtual outputs via virtual CRTCs (software-based). The desktop content is rendered normally, as if virtual outputs were physical displays.

4. **3D renderer reads and transforms**:
   - **GNOME**: Mutter can composite virtual outputs with 3D transformations applied
   - **XFCE4**: Breezy's 3D renderer reads the framebuffers from virtual outputs, applies 3D transformations (IMU-based head tracking, FOV calculations, display distance, etc.), and composites the result. XFCE4 doesn't have built-in 3D rendering capabilities, so Breezy provides its own renderer.

5. **Final output to physical display**: The 3D-rendered content is sent to the physical XR display (which is hidden from Display Settings). This is where the user sees the final result.

6. **Cursor handling**: The system cursor can be hidden on virtual outputs (since they're software-based), eliminating the cursor duplication issue. Only Breezy's 3D-rendered cursor is visible.

This approach maintains compatibility with existing X11 infrastructure while providing the flexibility needed for advanced XR desktop experiences.

---

## References

- **Extension Source**: `breezy-desktop/gnome/src/`
- **XR Driver**: `XRLinuxDriver/`
- **X11 Virtual Connector Design**: `breezy-desktop/doc_xfce4_xorg_xr_connector_design.md`
- **Mutter DisplayConfig**: `breezy-desktop/gnome/src/dbus-interfaces/org.gnome.Mutter.DisplayConfig.xml`
- **Shader Code**: `breezy-desktop/modules/sombrero/Sombrero.frag`
- **Xorg Virtual XR Implementation**: `xserver/hw/xfree86/drivers/modesetting/drmmode_xr_virtual.c`

