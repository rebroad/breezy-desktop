# Standalone Renderer Implementation Status

**Note**: This renderer is not specific to X11 - it works with any desktop environment/WM that uses Xorg (X11, i3, Openbox, etc.). It's called "standalone" because it runs as a separate process, independent of the compositor.

## Completed ✅

1. **C/C++ Renderer Skeleton** - High-performance renderer architecture with:
   - Lock-free ring buffer for frame transfer
   - Separate capture and render threads
   - Direct OpenGL rendering (no abstractions)

2. **DRM/KMS Capture Implementation** (`drm_capture.c`):
   - Finds virtual XR connector (XR-0) via DRM device enumeration
   - Maps framebuffer memory for direct access
   - Captures frames without X11 overhead
   - **Status**: Basic implementation complete, needs testing with actual virtual connector

3. **Build System** - Makefile configured with:
   - libdrm dependencies
   - OpenGL/GLX/EGL support
   - Threading support

4. **Test Script Updates** - `test_xr_virtual.sh` now:
   - Supports GNOME sessions
   - Tests breezy-desktop workflow
   - Monitors virtual connector creation

5. **Documentation** - Updated `BREEZY_X11_TECHNICAL.md` with new architecture

## In Progress 🔨

### 1. Virtual XR Connector in Xorg Modesetting Driver
**File**: `/home/rebroad/src/xserver/hw/xfree86/drivers/modesetting/drmmode_xr_virtual.c`

**What's needed**:
- Create virtual connector "XR-0" that appears in RandR
- Implement off-screen pixmap/framebuffer for virtual connector
- Expose framebuffer via DRM API so renderer can access it
- Mark physical XR display as `non_desktop` when AR mode enabled

**Current Status**: Design documented, implementation needed

### 2. Shader Loading (`shader_loader.c` - TODO)
**What's needed**:
- Load `Sombrero.frag` directly (it's already GLSL!)
- Create simple vertex shader for fullscreen quad
- Compile and link shader program
- Set up shader uniforms (IMU data, display settings)

**Note**: `modules/sombrero/Sombrero.frag` can be used directly - no porting needed!

### 3. OpenGL Context Creation
**What's needed**:
- Open X display
- Find AR glasses display output
- Create GLX or EGL context on that display
- Create fullscreen window or use display directly
- Enable vsync to match glasses refresh rate

### 4. IMU Data Reader
**What's needed**:
- Open `/dev/shm/breezy_desktop_imu`
- Parse binary format (see `gnome/src/devicedatastream.js` for layout)
- Extract quaternion, position, timestamps
- Thread-safe access (mutex protection)

**Data Layout** (from devicedatastream.js):
- Version (uint8)
- Enabled (bool)
- Look ahead config (4 floats)
- Display resolution (2 uints)
- Display FOV (float)
- Lens distance ratio (float)
- SBS enabled (bool)
- Custom banner enabled (bool)
- Smooth follow enabled (bool)
- Smooth follow origin data (16 floats)
- Pose position (3 floats)
- Epoch MS (2 uints)
- Pose orientation (16 floats)
- Parity byte (uint8)

## Next Steps for Testing 🧪

### Phase 1: Virtual Connector (Requires Xorg Changes)
1. Implement virtual XR connector in modesetting driver
   - XR-Manager output for control (must appear in xrandr)
   - CREATE_XR_OUTPUT property to create XR-0, XR-1, etc.
2. Test connector creation via xrandr (manual testing):
   ```bash
   xrandr --output XR-Manager --set CREATE_XR_OUTPUT "XR-0:1920:1080:60"
   xrandr --output XR-0 --auto
   ```
3. Verify connector appears in `xrandr --listoutputs`
4. Verify framebuffer is accessible via DRM
5. **Note**: In production, breezy-desktop will create XR-0 automatically after calibration

### Phase 2: Capture Testing
1. Build renderer: `cd x11/renderer && make`
2. Run with test mode (will fail until virtual connector exists):
   ```bash
   ./breezy_x11_renderer 1920 1080 60 90
   ```
3. Verify DRM device detection works
4. Test frame capture once connector is available

### Phase 3: Rendering Testing
1. Load Sombrero shader
2. Create OpenGL context on AR glasses display
3. Test 3D transformations with dummy IMU data
4. Verify vsync matches glasses refresh rate

### Phase 4: Integration Testing
1. Connect glasses → XRLinuxDriver detects
2. Calibration period completes (15 seconds, monitored by breezy-desktop)
3. **breezy-desktop detects calibration completion**
4. **breezy-desktop requests XR-0 creation** via XR-Manager
5. Physical display marked non-desktop (via EDID or RandR property)
6. Virtual connector XR-0 appears in xrandr
7. **breezy-desktop starts renderer** (spawns breezy_x11_renderer process)
8. Renderer captures from XR-0
9. 3D rendering to physical display works
10. **Important**: Renderer must NOT start before calibration completes (potential drift otherwise)

## When You Need My Involvement

**Testing Required**:
1. **Virtual Connector Creation**: After implementing `drmmode_xr_virtual.c`, test that:
   - XR-0 appears in xrandr
   - Framebuffer is accessible
   - Desktop content renders to it

2. **DRM Capture**: Test that renderer can:
   - Find XR-0 connector
   - Map framebuffer
   - Capture frames successfully

3. **3D Rendering**: Test that:
   - OpenGL context works on AR glasses display
   - Shaders compile and run
   - 3D transformations look correct
   - Frame rate matches refresh rate

**Feedback Needed**:
- Does virtual connector appear correctly?
- Are frames being captured? (check console output)
- Does 3D rendering look correct?
- Any performance issues?
- Any visual artifacts?

## Integration Notes

### breezy-desktop Integration Requirements

The renderer is **NOT** a standalone tool - it must be integrated with breezy-desktop:

**Who does what:**
- **XRLinuxDriver**: Provides IMU data and calibration state (does NOT create displays)
- **breezy-desktop**: Creates XR-0 and starts renderer (after calibration completes)
- **breezy_x11_renderer** (standalone binary): Captures from XR-0 and renders 3D content
  - Works with any Xorg-based desktop/WM (X11, i3, Openbox, etc.)
  - Cannot use compositor-specific code (e.g., GNOME's `virtualdisplayeffect.js` or KWin's QML) because it's a standalone C binary

**Integration steps:**

1. **Calibration Detection**: breezy-desktop must monitor XRLinuxDriver state to detect when calibration completes
   - Check calibration state in shared memory or via IPC
   - Wait for calibration period to finish (15 seconds for XREAL devices)

2. **Virtual Connector Creation**: After calibration, breezy-desktop calls:
   ```bash
   xrandr --output XR-Manager --set CREATE_XR_OUTPUT "XR-0:1920:1080:60"
   ```
   - Requires XR-Manager to exist (virtual connector implementation in Xorg modesetting driver)

3. **AR Mode Enablement**: After creating XR-0, breezy-desktop enables AR mode:
   ```bash
   xrandr --output XR-Manager --set AR_MODE 1
   ```
   - This hides the physical XR connector and shows the virtual XR connector(s)
   - AR_MODE is a global setting on XR-Manager (not on individual XR outputs)

3. **Renderer Startup**: breezy-desktop spawns renderer process:
   ```python
   renderer_process = subprocess.Popen(
       ['breezy_x11_renderer', '1920', '1080', '60', '90'],
       start_new_session=True
   )
   ```

4. **Error Handling**: breezy-desktop must monitor renderer process and restart if it crashes

5. **Shutdown**: When breezy-desktop stops or glasses disconnect:
   - Stop renderer process
   - Optionally delete XR-0 virtual connector

**Implementation Location**: 
- `ui/src/virtualdisplaymanager.py` - Already spawns virtual display processes (could be extended)
- Or new X11-specific integration code

## Files Created/Modified

### New Files:
- `x11/renderer/breezy_x11_renderer.c` - Main renderer (standalone binary)
- `x11/renderer/breezy_x11_renderer.h` - Header
- `x11/renderer/drm_capture.c` - DRM/KMS capture
- `x11/renderer/imu_reader.c` - IMU data reader
- `x11/renderer/shader_loader.c` - Shader loading
- `x11/renderer/opengl_context.c` - OpenGL context creation
- `x11/renderer/Makefile` - Build system
- `x11/renderer/IMPLEMENTATION_STATUS.md` - This file
- `x11/renderer/TESTING_GUIDE.md` - Testing instructions
- `x11/renderer/PIPEWIRE_VS_DRM.md` - Architecture comparison

### Modified Files:
- `BREEZY_X11_TECHNICAL.md` - Updated architecture docs
- `xserver/test_xr_virtual.sh` - GNOME support, breezy-desktop workflow

### TODO Files (to be created):
- `x11/renderer/shader_loader.c` - Load and compile `modules/sombrero/Sombrero.frag` directly (no duplication)
- `x11/renderer/imu_reader.c` - Parse IMU shared memory
- `x11/renderer/opengl_context.c` - Create OpenGL context on AR display
- `xserver/hw/xfree86/drivers/modesetting/drmmode_xr_virtual.c` - Virtual connector

