# Testing Guide for XFCE4 Renderer

## Prerequisites

Before testing, ensure you have:

1. **XRLinuxDriver running**:
   ```bash
   systemctl --user status xr-driver
   ```

2. **AR glasses connected** and detected by XRLinuxDriver

3. **Calibration completed** - XRLinuxDriver must finish calibration (typically 15 seconds for XREAL devices) before virtual display is created

4. **breezy-desktop service running** - The renderer is started automatically by breezy-desktop after calibration completes

5. **Virtual XR connector implementation** - XR-Manager must be available in xrandr (XR-0 will be created on-demand by breezy-desktop)

6. **Dependencies installed**:
   ```bash
   sudo apt-get install build-essential libdrm-dev libgl1-mesa-dev libglx-dev libegl1-mesa-dev libx11-dev
   ```

## Building

```bash
cd /home/rebroad/src/breezy-desktop/xfce4/renderer
make
```

This should create `breezy_xfce4_renderer` executable.

## Architecture Overview

### Components and Responsibilities

1. **XRLinuxDriver** (service):
   - Detects AR glasses hardware
   - Provides IMU data via `/dev/shm/breezy_desktop_imu`
   - Manages calibration state (15 second period)
   - **Does NOT create virtual displays** - only provides IMU data

2. **breezy-desktop** (GUI/service):
   - Monitors XRLinuxDriver calibration state
   - **Creates XR-0 virtual connector** - Calls `xrandr --output XR-Manager --set CREATE_XR_OUTPUT "XR-0:WIDTH:HEIGHT:REFRESH"`
   - **Starts the renderer process** - Spawns `breezy_xfce4_renderer` binary
   - Manages renderer lifecycle (restart on crash, cleanup on disconnect)

3. **breezy_xfce4_renderer** (separate binary):
   - **Captures 2D frames** from XR-0 virtual connector via DRM/KMS
   - **Applies 3D transformations** using IMU data and GLSL shaders
   - **Renders to AR glasses** physical display via OpenGL

## Expected Workflow

The renderer is **NOT** meant to be run manually. Instead:

1. **Start breezy-desktop** (GUI or service)
2. **Connect AR glasses** - XRLinuxDriver detects device
3. **Calibration period** - 15 seconds (XREAL devices) for IMU calibration
   - XRLinuxDriver provides calibration state
4. **breezy-desktop detects calibration completion** - Monitors XRLinuxDriver state
5. **breezy-desktop creates XR-0** - Calls xrandr via XR-Manager (requires virtual connector implementation in Xorg)
6. **breezy-desktop starts renderer** - Spawns `breezy_xfce4_renderer` process
7. **Renderer captures from XR-0** and renders 3D transformed content to AR glasses

**Important**: The renderer should NOT run without calibration first, as this would cause severe IMU drift.

## Testing Steps

### Phase 1: Manual Testing (Development Only)

For development/debugging, you can run the renderer manually, but you must ensure:

1. **Calibration has completed**:
   - Wait for XRLinuxDriver calibration period
   - Check that IMU data is valid in `/dev/shm/breezy_desktop_imu`

2. **XR-Manager exists** (virtual connector implementation must be complete):
   ```bash
   xrandr --listoutputs | grep XR-Manager
   ```

3. **Create XR-0 virtual connector** (normally done by breezy-desktop):
   ```bash
   xrandr --output XR-Manager --set CREATE_XR_OUTPUT "XR-0:1920:1080:60"
   xrandr --output XR-0 --auto
   ```

4. **Verify XR-0 exists**:
   ```bash
   xrandr --listoutputs | grep XR-0
   ```

5. **Run renderer manually** (for testing only):
   ```bash
   ./breezy_xfce4_renderer 1920 1080 60 90
   ```
   
   Parameters:
   - `1920 1080`: Virtual display resolution
   - `60`: Capture framerate (Hz)
   - `90`: Render framerate (Hz) - should match AR glasses refresh rate

3. **Expected console output**:
   ```
   Breezy XFCE4 Renderer
   Virtual display: 1920x1080@60Hz
   Render rate: 90Hz
   [IMU] Reader initialized, mapped XXX bytes
   [DRM] Opened device: /dev/dri/cardX
   [DRM] Mapped framebuffer: 1920x1080, pitch=XXXX, size=XXXXX
   [GLX] OpenGL context created successfully
   [GLX] OpenGL version: X.X
   [GLX] VSync enabled
   [Shader] Shaders loaded and compiled successfully
   [Render] Render thread initialized successfully
   [Capture] Thread started for 1920x1080@60Hz
   [Render] Thread started at 90Hz
   Renderer running. Press Ctrl+C to stop.
   ```

### Phase 2: What to Look For

#### Success Indicators ✅

- No error messages during initialization
- Renderer runs continuously (doesn't crash)
- Console shows frame capture/render messages periodically
- AR glasses display shows content (even if incorrect/transformed)
- VSync is enabled (smooth rendering, no tearing)

#### Potential Issues ⚠️

1. **"Failed to open /dev/shm/breezy_desktop_imu"**:
   - XRLinuxDriver might not be running
   - Check: `systemctl --user status xr-driver`
   - Start: `systemctl --user start xr-driver`

2. **"Failed to find DRM device with connector XR-0"**:
   - XR-0 hasn't been created yet - breezy-desktop must request it first
   - Calibration must be complete before XR-0 is created
   - Check: `xrandr --listoutputs | grep XR-Manager` (should exist if virtual connector code is complete)
   - Check: `xrandr --listoutputs | grep XR-0` (should exist after breezy-desktop requests it)

3. **"Failed to create OpenGL context"**:
   - X11 display might not be available
   - Check: `echo $DISPLAY` should show `:0` or similar
   - GPU drivers might not be installed correctly

4. **"Failed to load shaders"** or shader compile errors:
   - Sombrero.frag might not be found
   - Check path: `ls ../modules/sombrero/Sombrero.frag`
   - Shader might have syntax errors (check console output)

5. **Black screen on AR glasses**:
   - Normal if virtual connector doesn't exist yet
   - Could also indicate rendering issue
   - Check console for errors

6. **High CPU usage**:
   - Expected during development
   - Will be optimized once DMA-BUF/zero-copy is implemented

### Phase 3: Integration Testing (Proper Workflow)

Once virtual connector and breezy-desktop integration is complete:

1. **Start XFCE4 session** (or use test script):
   ```bash
   cd /home/rebroad/src/xserver
   ./test_xr_virtual.sh xfce4
   ```

2. **Launch breezy-desktop** (GUI or service)

3. **Connect AR glasses** - XRLinuxDriver detects device

4. **Wait for calibration** - 15 seconds (monitor in breezy-desktop UI or XRLinuxDriver logs)

5. **breezy-desktop automatically**:
   - Detects calibration completion
   - Creates XR-0 via XR-Manager
   - Starts renderer process

6. **Verify XR-0 exists**:
   ```bash
   DISPLAY=:1 xrandr --listoutputs | grep XR-0
   ```

7. **Verify renderer is running**:
   ```bash
   ps aux | grep breezy_xfce4_renderer
   ```

8. **Expected behavior**:
   - Desktop content appears on AR glasses
   - 3D transformations applied based on IMU data
   - Head tracking works smoothly (no drift, as calibration completed first)
   - VSync matches glasses refresh rate

### Phase 4: Feedback Needed

When testing, please provide feedback on:

1. **Does it compile?** Any build errors?

2. **Does it run?** Any runtime errors in console?

3. **DRM capture**: Can it find XR-0 connector? (This won't work until virtual connector is implemented)

4. **OpenGL context**: Does it create successfully? What GPU/driver?

5. **Shader loading**: Does Sombrero.frag load and compile?

6. **Rendering**: Do you see anything on AR glasses? (Even if wrong/transformed)

7. **Performance**: CPU usage? Frame rate smooth?

8. **Visual artifacts**: Any glitches, tearing, incorrect transformations?

## Integration with breezy-desktop

The renderer is designed to be started by **breezy-desktop**, not run manually.

### breezy-desktop Integration Requirements

breezy-desktop needs to:

1. **Monitor calibration state** - Check XRLinuxDriver calibration state (via shared memory or IPC)
2. **Create XR-0 virtual connector** - After calibration completes:
   ```python
   subprocess.run(['xrandr', '--output', 'XR-Manager', 
                   '--set', 'CREATE_XR_OUTPUT', 'XR-0:1920:1080:60'])
   subprocess.run(['xrandr', '--output', 'XR-0', '--auto'])
   ```
3. **Start renderer process** - Spawn as separate binary:
   ```python
   renderer_process = subprocess.Popen(
       ['breezy_xfce4_renderer', '1920', '1080', '60', '90'],
       start_new_session=True
   )
   ```
4. **Monitor renderer** - Restart if it crashes, cleanup on disconnect

### Implementation Location

Integration code should go in:
- `ui/src/virtualdisplaymanager.py` - Already spawns virtual display processes
- Or new XFCE4-specific backend code

**Current Status**: 
- ✅ Renderer implemented as separate binary
- ⏳ breezy-desktop integration pending (detecting calibration completion, creating XR-0 via XR-Manager, spawning renderer)

## Current Limitations

1. **Virtual connector not yet implemented** - XR-Manager and XR-0 creation in Xorg modesetting driver
2. **breezy-desktop integration pending** - Needs to detect calibration completion and start renderer
3. **Shader uniforms not fully set** - Only basic texture rendering works
4. **IMU data not fully integrated** - 3D transformations not applied yet
5. **Format conversion** - Assumes RGBA format, might need adjustment

## Next Steps After Testing

Once basic functionality works:
1. Complete virtual connector implementation
2. Add all shader uniforms
3. Integrate IMU data fully
4. Add format detection/conversion
5. Optimize with DMA-BUF if needed

