# Virtual Desktop Size Increase and Follow Feature Implementation

This document shows where the code that increases virtual desktop/screen size when looking at it, and the "follow" feature that repositions the desktop in virtual space, are implemented across GNOME, KWin, and X11 backends.

---

## 1. Virtual Desktop Size Increase (Zoom When Looking)

The virtual desktop size increases when you look at it by adjusting the `display_distance` parameter. When a monitor is "focused" (you're looking at it), it uses a closer `display_distance` value, which makes it appear larger. This is done via scaling the monitor position vectors.

### GNOME Implementation

**Location:** `gnome/src/virtualdisplayeffect.js`

**Key Function - Distance Update:**
```268:322:gnome/src/virtualdisplayeffect.js
    _update_display_distance() {
        const desired_distance = this._is_focused() ? this.display_distance : this.display_distance_default;
        if (this._distance_ease_timeline?.is_playing()) {
            // we're already easing towards the desired distance, do nothing
            if (this._distance_ease_target === desired_distance) return;

            this._distance_ease_timeline.stop();
        }

        if (this.no_distance_ease) {
            this._current_display_distance = desired_distance;
            this._update_display_position();
            this.no_distance_ease = false;
            return;
        }

        // if we're the focused display, we'll double the timeline and wait for the first half to let other
        // displays ease out first
        this._distance_ease_focus = this._is_focused();
        const ease_out_timeline_ms = 150;
        const pause_ms = 50;
        const ease_in_timeline_ms = 500; // includes ease out and pause
        const ease_in_begin_pct = (ease_out_timeline_ms + pause_ms) / ease_in_timeline_ms;
        const timeline_ms = this._distance_ease_focus ?
            ease_in_timeline_ms :
            ease_out_timeline_ms;

        this._distance_ease_start = this._current_display_distance;
        this._distance_ease_timeline = Clutter.Timeline.new_for_actor(this.get_actor(), timeline_ms);

        this._distance_ease_target = desired_distance;
        this._distance_ease_timeline.connect('new-frame', (() => {
            let progress = this._distance_ease_timeline.get_progress();
            if (this._distance_ease_focus) {
                // if we're the focused display, wait for the first half of the timeline to pass
                if (progress < ease_in_begin_pct) return;

                // treat the second half of the timeline as its own full progression
                progress = (progress - ease_in_begin_pct) / (1 - ease_in_begin_pct);

                // put this display in front as it starts to easy in
                this.is_closest = true;
            } else {
                this.is_closest = false;
            }

            this._current_display_distance = this._distance_ease_start +
                (1 - Math.cos(progress * Math.PI)) / 2 * (this._distance_ease_target - this._distance_ease_start);
            this._update_display_position();
        }).bind(this));

        this._distance_ease_timeline.start();

        if (this.smooth_follow_enabled) this._handle_smooth_follow_enabled_update();
    }
```

**Key Function - Position Update (Applies Distance Scaling):**
```381:399:gnome/src/virtualdisplayeffect.js
    _update_display_position() {
        // this is in NWU coordinates
        const monitorPlacement = this.monitor_placements[this.monitor_index];
        const noRotationVector = monitorPlacement.centerNoRotate.map(coord => coord * this._current_display_distance / this.display_distance_default);
        const inverse_follow_ease = 1.0 - this._current_follow_ease_progress;
        let finalPositionVector = noRotationVector;
        if (this._current_follow_ease_progress > 0.0)  {
            // slerp from the rotated display to the centered display
            finalPositionVector = noRotationVector.map(coord => coord * inverse_follow_ease);
            finalPositionVector[0] = noRotationVector[0];
        }
        this._vertices = createVertexMesh(this.fov_details, this.monitor_details, finalPositionVector);

        const rotation_radians = this.monitor_placements[this.monitor_index].rotationAngleRadians;
        if (this._initialized) {
            this.set_uniform_float(this.get_uniform_location("u_rotation_x_radians"), 1, [rotation_radians.x * inverse_follow_ease]);
            this.set_uniform_float(this.get_uniform_location("u_rotation_y_radians"), 1, [rotation_radians.y * inverse_follow_ease]);
        }
    }
```

**Line 384** is the key: `coord * this._current_display_distance / this.display_distance_default` - this scales the monitor position vectors based on distance, making focused monitors appear larger (closer = larger).

**Extension - Monitors Size Adjustment:**
```400:413:gnome/src/extension.js
    _update_display_distance(object, event) {
        const value = this.settings.get_double('display-distance');
        Globals.logger.log_debug(`BreezyDesktopExtension _update_display_distance ${value}`);
        if (value !== undefined) {
            let focusedMonitorSizeAdjustment = 1.0;
            if (this._virtual_displays_actor?.focused_monitor_details && this._target_monitor) {
                const fovMonitor = this._target_monitor.monitor;
                const focusedMonitor = this._virtual_displays_actor.focused_monitor_details;
                focusedMonitorSizeAdjustment =
                    Math.max(focusedMonitor.width / fovMonitor.width, focusedMonitor.height / fovMonitor.height);
            }
            this._write_control('breezy_desktop_display_distance', value / focusedMonitorSizeAdjustment);
        }
    }
```

---

### KWin Implementation

**Location:** `kwin/src/qml/BreezyDesktop.qml` and `kwin/src/breezydesktopeffect.cpp`

**Key Function - Focus Update (Triggers Zoom):**
```28:111:kwin/src/qml/BreezyDesktop.qml
    function updateFocus(smoothFollowEnabledChanged = false) {
        const orientations = smoothFollowEnabled ? effect.smoothFollowOrigin : effect.poseOrientations;
        if (orientations && orientations.length > 0) {
            let focusedIndex = -1;
            const lookingAtIndex = displays.findFocusedMonitor(
                displays.eusToNwuQuat(orientations[0]),
                breezyDesktop.monitorPlacements.map(monitorVectors => monitorVectors.centerLook),
                breezyDesktop.focusedMonitorIndex,
                smoothFollowEnabled,
                breezyDesktop.fovDetails,
                breezyDesktop.screens.map(screen => screen.geometry)
            );

            if (breezyDesktop.lookingAtMonitorIndex !== lookingAtIndex) {
                breezyDesktop.lookingAtMonitorIndex = lookingAtIndex;
                effect.lookingAtScreenIndex = lookingAtIndex;
            }

            if (effect.zoomOnFocusEnabled || smoothFollowEnabled) {
                focusedIndex = lookingAtIndex;
            }

            let focusedDisplay;
            let unfocusedDisplay;
            let startSmoothFollowFocusAnimation = false;
            if (smoothFollowEnabledChanged) {
                let targetDisplay;
                let targetProgress;
                if (smoothFollowEnabled && focusedIndex !== -1) {
                    focusedDisplay = breezyDesktop.displayAtIndex(focusedIndex);
                    if (focusedDisplay) {
                        targetDisplay = focusedDisplay;
                        targetProgress = 1.0;
                        startSmoothFollowFocusAnimation = true;
                    }
                } else if (!smoothFollowEnabled && breezyDesktop.focusedMonitorIndex !== -1) {
                    unfocusedDisplay = breezyDesktop.displayAtIndex(breezyDesktop.focusedMonitorIndex);
                    if (unfocusedDisplay) {
                        targetDisplay = unfocusedDisplay;
                        targetProgress = 0.0;
                    }
                }

                if (targetDisplay) {
                    smoothFollowTransitionAnimation.stop();
                    smoothFollowTransitionAnimation.target = targetDisplay;
                    smoothFollowTransitionAnimation.from = targetDisplay.smoothFollowTransitionProgress;
                    smoothFollowTransitionAnimation.to = targetProgress;
                    smoothFollowTransitionAnimation.start();
                }
            }

            if (focusedIndex !== breezyDesktop.focusedMonitorIndex) {
                const unfocusedIndex = breezyDesktop.focusedMonitorIndex;
                if (!focusedDisplay) focusedDisplay = focusedIndex !== -1 ? breezyDesktop.displayAtIndex(focusedIndex) : null;
                if (!focusedDisplay) {
                    if (!unfocusedDisplay) unfocusedDisplay = breezyDesktop.displayAtIndex(unfocusedIndex);
                    if (unfocusedDisplay) {
                        zoomOutAnimation.target = unfocusedDisplay;
                        zoomOutAnimation.target.targetDistance = effect.allDisplaysDistance;
                        zoomOutAnimation.start();
                    }
                } else {
                    if (!unfocusedDisplay) unfocusedDisplay = unfocusedIndex !== -1 ? breezyDesktop.displayAtIndex(unfocusedIndex) : null;
                    if (!unfocusedDisplay) {
                        zoomInAnimation.target = focusedDisplay;
                        focusedDisplay.targetDistance = effect.focusedDisplayDistance;
                        zoomInAnimation.start();
                    } else {
                        zoomInSeqAnimation.target = focusedDisplay;
                        focusedDisplay.targetDistance = effect.focusedDisplayDistance;

                        zoomOutSeqAnimation.target = unfocusedDisplay;
                        unfocusedDisplay.targetDistance = effect.allDisplaysDistance;

                        zoomOnFocusSequence.start();
                    }
                }
                breezyDesktop.focusedMonitorIndex = focusedIndex;
            }

            if (startSmoothFollowFocusAnimation) smoothFollowFocusedAnimation.restart();
        }
    }
```

**Lines 86-88, 93-95, 97-103** show the zoom animations - setting `targetDistance` to `effect.focusedDisplayDistance` (zoomed in) or `effect.allDisplaysDistance` (zoomed out).

**Key Function - Display Distance Calculation with Size Adjustment:**
```790:814:kwin/src/breezydesktopeffect.cpp
void BreezyDesktopEffect::updateDriverSmoothFollowSettings() {
    qreal adjustedDistance = m_focusedDisplayDistance;

    if (m_lookingAtScreenIndex != -1 && !m_displayResolution.isEmpty()) {
        // Adjust display distance by relative monitor size compared to the FOV monitor
        const Output *focusedOutput = effects->screens().at(m_lookingAtScreenIndex);
        const QSize focusedSize = focusedOutput ? focusedOutput->geometry().size() : QSize();

        if (focusedSize.isValid()) {
            const qreal fovW = static_cast<qreal>(m_displayResolution.at(0));
            const qreal fovH = static_cast<qreal>(m_displayResolution.at(1));

            const qreal ratioW = static_cast<qreal>(focusedSize.width()) / fovW;
            const qreal ratioH = static_cast<qreal>(focusedSize.height()) / fovH;
            const qreal focusedMonitorSizeAdjustment = std::max(ratioW, ratioH);

            adjustedDistance = m_focusedDisplayDistance / focusedMonitorSizeAdjustment;
        }
    }

    QJsonObject flags;
    flags.insert(QStringLiteral("breezy_desktop_display_distance"), adjustedDistance);
    flags.insert(QStringLiteral("breezy_desktop_follow_threshold"), m_smoothFollowThreshold);
    XRDriverIPC::instance().writeControlFlags(flags);
}
```

**Display Position Calculation (Distance Applied):**
```113:120:kwin/src/qml/BreezyDesktop.qml
    function displayRotationVector(display) {
        const displayNwu =
            display.monitorPlacement.centerNoRotate
                                    .times(display.monitorDistance / effect.allDisplaysDistance);

        const eusVector = displays.nwuToEusVector(displayNwu)
        return display.rotationMatrix.times(eusVector);
    }
```

**Line 116** applies the distance scaling: `.times(display.monitorDistance / effect.allDisplaysDistance)`.

---

### X11 Implementation

**Status:** Not yet implemented in X11 backend.

The X11 renderer (`x11/renderer/`) reads `smooth_follow_enabled` and `smooth_follow_origin` from shared memory, but **does not currently implement the display distance/zoom feature**. The shader would need to be updated to apply distance scaling similar to GNOME/KWin.

**Current X11 Renderer State:**
- Reads smooth follow data: `x11/renderer/imu_reader.c` lines 199-203
- Stores in config: `x11/renderer/breezy_x11_renderer.h` lines 114-115
- But no distance scaling logic implemented in shader

---

## 2. Follow Feature (Desktop Repositioning)

The "follow" feature repositions the desktop in virtual space when you look beyond it by an adjustable threshold. This is implemented via "smooth follow" in the XR Linux Driver, which smoothly repositions the origin point based on head rotation when it exceeds a threshold.

### GNOME Implementation

**Location:** `gnome/src/virtualdisplayeffect.js`

**Key Function - Smooth Follow Handling:**
```324:377:gnome/src/virtualdisplayeffect.js
    _handle_smooth_follow_enabled_update() {
        // we'll re-trigger this once a monitor becomes focused
        if (this.focused_monitor_index === -1) return;

        this._use_smooth_follow_origin = false;

        if (this._follow_ease_timeline?.is_playing()) this._follow_ease_timeline.stop();

        const ease_to_focus = this.smooth_follow_enabled && this._is_focused();
        const from = this._current_follow_ease_progress;
        const to = ease_to_focus ? 1.0 : 0.0;
        const toggleTime = this.smooth_follow_toggle_epoch_ms === 0 ? Date.now() : this.smooth_follow_toggle_epoch_ms;

        // would have been a slight delay between request and slerp actually starting
        const toggleDelayMs = (Date.now() - toggleTime) * 0.75;
        const slerpStartTime = toggleTime + toggleDelayMs;

        if (ease_to_focus || from > 0.0) {
            this._follow_ease_timeline = Clutter.Timeline.new_for_actor(
                this.get_actor(),
                SMOOTH_FOLLOW_SLERP_TIMELINE_MS - toggleDelayMs
            );
            this._follow_ease_timeline.connect('new-frame', ((timeline, elapsed_ms) => {
                const toggleTimeOffsetMs = Date.now() - slerpStartTime;

                // this relies on the slerp function tuned to reach 100% in about 1 second
                const progress = smoothFollowSlerpProgress(toggleTimeOffsetMs);
                this._current_follow_ease_progress = from + (to - from) * progress;
                this._update_display_position();
            }).bind(this));

            this._follow_ease_timeline.connect('completed', (() => {
                this._current_follow_ease_progress = to;
                this._use_smooth_follow_origin = false;
                this.smooth_follow_toggle_epoch_ms = 0;
                this._update_display_position();
            }).bind(this));

            this._follow_ease_timeline.start();
        } else if (!this.smooth_follow_enabled) {
            // smooth follow has been turned off and this screen wasn't the focus,
            // continue to use the smooth_follow_origin data for 1 more second
            this._use_smooth_follow_origin = true;
            GLib.timeout_add(
                GLib.PRIORITY_DEFAULT,
                SMOOTH_FOLLOW_SLERP_TIMELINE_MS - toggleDelayMs,
                (() => {
                    this._use_smooth_follow_origin = false;
                    this.smooth_follow_toggle_epoch_ms = 0;
                    this._current_follow_ease_progress = to;
                    return GLib.SOURCE_REMOVE;
                }).bind(this)
            );
        }
    }
```

**Key Function - Pose Application (Uses Smooth Follow Origin):**
```566:583:gnome/src/virtualdisplayeffect.js
        if (this.imu_snapshots && !this.show_banner) {
            let lookAheadSet = false;
            if (!this._use_smooth_follow_origin && (!this.smooth_follow_enabled || this._is_focused() || this._current_follow_ease_progress > 0.0)) {
                if (this._current_follow_ease_progress > 0.0 && this._current_follow_ease_progress < 1.0) {
                    // don't apply look-ahead while the display is slerping
                    this.set_uniform_float(this.get_uniform_location('u_look_ahead_ms'), 1, [0.0]);
                    lookAheadSet = true;
                }

                this.set_uniform_matrix(this.get_uniform_location("u_pose_orientation"), false, 4, this.imu_snapshots.pose_orientation);
                this.set_uniform_float(this.get_uniform_location("u_pose_position"), 3, posePositionPixels);
            } else {
                this.set_uniform_matrix(this.get_uniform_location("u_pose_orientation"), false, 4, this.imu_snapshots.smooth_follow_origin);
                this.set_uniform_float(this.get_uniform_location("u_pose_position"), 3, [0.0, 0.0, 0.0]);
            }
```

**Line 578** shows that when smooth follow is active, it uses `this.imu_snapshots.smooth_follow_origin` instead of `this.imu_snapshots.pose_orientation`. The `smooth_follow_origin` is computed by the XR Linux Driver based on the follow threshold.

**Extension - Follow Threshold Update:**
```415:419:gnome/src/extension.js
    _update_follow_threshold(settings, event) {
        const value = settings.get_double('follow-threshold');
        Globals.logger.log_debug(`BreezyDesktopExtension _update_follow_threshold ${value}`);
        if (value !== undefined) this._write_control('breezy_desktop_follow_threshold', value);
    }
```

---

### KWin Implementation

**Location:** `kwin/src/qml/BreezyDesktop.qml` and `kwin/src/breezydesktopeffect.cpp`

**Key Function - Smooth Follow Position Calculation:**
```129:154:kwin/src/qml/BreezyDesktop.qml
    function displaySmoothFollowVector(display, smoothFollowRotation) {
        // for smooth follow, place the display centered directly in front of the camera
        const displayDistanceNorth =
            display.monitorPlacement.monitorCenterNorth *
            display.monitorDistance / effect.allDisplaysDistance;
        const eusVector = Qt.vector3d(0, 0, -displayDistanceNorth);

        return smoothFollowRotation.times(eusVector);
    }

    // don't call this from the delegate to avoid binding the position property to the effect properties
    // used for smooth follow
    function displayPosition(display, smoothFollowRotation) {
        // short circuit to avoid slerping if not needed
        if (display.smoothFollowTransitionProgress === 1.0) {
            return displaySmoothFollowVector(display, smoothFollowRotation);
        }

        const finalPosition = displays.slerpVector(
            displayRotationVector(display),
            displaySmoothFollowVector(display, smoothFollowRotation),
            display.smoothFollowTransitionProgress
        );

        return finalPosition
    }
```

**Smooth Follow Quaternion Calculation:**
```125:127:kwin/src/qml/BreezyDesktop.qml
    function smoothFollowQuat() {
        return effect.smoothFollowOrigin[0].times(effect.poseOrientations[0].conjugated());
    }
```

**Lines 125-136** show how smooth follow repositions displays: it uses `smoothFollowOrigin` (from the driver) to calculate a rotation that keeps the display centered in front of the camera.

**Driver Settings Update (Includes Follow Threshold):**
```810:813:kwin/src/breezydesktopeffect.cpp
    QJsonObject flags;
    flags.insert(QStringLiteral("breezy_desktop_display_distance"), adjustedDistance);
    flags.insert(QStringLiteral("breezy_desktop_follow_threshold"), m_smoothFollowThreshold);
    XRDriverIPC::instance().writeControlFlags(flags);
```

---

### X11 Implementation

**Location:** `x11/renderer/imu_reader.c` and shader code (not yet fully implemented)

**Current State - Reads Smooth Follow Data:**
```199:203:x11/renderer/imu_reader.c
    // Read smooth follow enabled (1 bool)
    config.smooth_follow_enabled = data[OFFSET_SMOOTH_FOLLOW_ENABLED] != 0;

    // Read smooth follow origin (16 floats = 4x4 matrix)
    memcpy(config.smooth_follow_origin, &data[OFFSET_SMOOTH_FOLLOW_ORIGIN_DATA], sizeof(float) * 16);
```

**Status:** The X11 renderer reads the smooth follow data from shared memory, but **the shader does not yet apply it**. Similar to GNOME/KWin, the shader would need to:
1. Use `smooth_follow_origin` instead of `pose_orientation` when `smooth_follow_enabled` is true
2. Apply the smooth follow rotation to reposition the desktop

---

## Summary

### Virtual Desktop Size Increase (Zoom)

**GNOME:**
- **File:** `gnome/src/virtualdisplayeffect.js`
- **Key Lines:** 268-322 (`_update_display_distance`), 384 (distance scaling in `_update_display_position`)
- **Mechanism:** Scales monitor position vectors by `display_distance / display_distance_default`

**KWin:**
- **Files:** `kwin/src/qml/BreezyDesktop.qml`, `kwin/src/breezydesktopeffect.cpp`
- **Key Lines:** 86-103 (zoom animations), 116 (distance scaling), 790-814 (distance calculation with size adjustment)
- **Mechanism:** Animates `monitorDistance` property between `focusedDisplayDistance` and `allDisplaysDistance`

**X11:**
- **Status:** ❌ **Not implemented** - shader needs distance scaling logic

---

### Follow Feature

**GNOME:**
- **File:** `gnome/src/virtualdisplayeffect.js`
- **Key Lines:** 324-377 (`_handle_smooth_follow_enabled_update`), 578 (uses `smooth_follow_origin`)
- **Mechanism:** Uses `smooth_follow_origin` quaternion from driver instead of raw `pose_orientation`

**KWin:**
- **Files:** `kwin/src/qml/BreezyDesktop.qml`, `kwin/src/breezydesktopeffect.cpp`
- **Key Lines:** 125-154 (smooth follow position calculation), 810-813 (threshold sent to driver)
- **Mechanism:** Calculates smooth follow rotation from `smoothFollowOrigin` and applies it to display positions

**X11:**
- **Files:** `x11/renderer/imu_reader.c`
- **Key Lines:** 199-203 (reads smooth follow data)
- **Status:** ⚠️ **Partially implemented** - data is read but shader doesn't apply it yet

---

## Driver-Side Implementation

The actual threshold detection and smooth follow origin calculation happens in the **XRLinuxDriver**:

- **File:** `XRLinuxDriver/src/plugins/smooth_follow.c`
- **Key Function:** Lines 122-173 (`update_smooth_follow_params`)
- **Key Calculation:** Lines 151-163 (Breezy Desktop follow threshold calculation)

The driver computes `smooth_follow_origin` based on head rotation exceeding the threshold, and all three backends (GNOME, KWin, X11) read this value from shared memory.

