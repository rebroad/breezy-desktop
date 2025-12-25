---
name: Adapt Breezy Desktop for X11 with 3D Rendering
overview: Adapt breezy-desktop to support X11 on X11 by (1) extending the Xorg modesetting driver with a virtual XR connector and AR mode that can hide the physical glasses output from the XRandR 2D map, and (2) rendering virtual desktops in 3D space on AR glasses via a standalone Breezy OpenGL renderer that consumes those virtual surfaces and IMU data from XRLinuxDriver.
todos:
  - id: research_3d_rendering
    content: "Research 3D rendering requirements: study GNOME/KDE shader code, design standalone renderer architecture, choose implementation language (Python vs C/C++)"
    status: completed
  - id: create_x11_backend_structure
    content: Create breezy-desktop/x11/ directory structure (src/, renderer/, bin/) and backend interface files
    status: completed
    dependencies:
      - research_3d_rendering
  - id: implement_virtual_display_creation
    content: Implement virtual display creation for X11 by extending the Xorg modesetting driver with a virtual XR connector (seen by XRandR as a normal monitor) backed by off-screen surfaces, with no XRandR/dummy-driver hack
    status: pending
    dependencies:
      - create_x11_backend_structure
  - id: design_xorg_virtual_connector
    content: Research Xorg modesetting driver (hw/xfree86/drivers/modesetting) and Mutter's monitor stack to design a virtual XR connector that appears as an XRandR output but can be driven by Breezy's 3D renderer and, in AR mode, hides the physical XReal glasses connector from the 2D desktop map
    status: pending
    dependencies:
      - design_x11_virtual_display_api
  - id: implement_3d_renderer
    content: "Implement standalone 3D renderer with multi-threaded architecture: non-blocking X11 screen capture thread, render thread matching glasses refresh rate, thread-safe frame buffer, IMU data reading, GLSL shader porting, OpenGL rendering to AR glasses display"
    status: pending
    dependencies:
      - create_x11_backend_structure
  - id: integrate_with_ui
    content: Integrate X11 backend with breezy-desktop UI (detection, display management, renderer lifecycle)
    status: pending
    dependencies:
      - implement_virtual_display_creation
      - implement_3d_renderer
  - id: update_build_system
    content: Add X11 backend to build system, create installation scripts, update documentation
    status: pending
    dependencies:
      - integrate_with_ui
  - id: test_and_validate
    content: Test virtual display creation, 3D rendering in AR glasses, follow mode, and all XR driver controls
    status: pending
    dependencies:
      - update_build_system
---

# Adapt Breezy Desktop for X11 with 3D Rendering

## Critical Requirement: 3D Transformations

**The Problem:** Virtual displays MUST be rendered in 3D space in AR glasses. This is not optional - it's the core functionality.

**How It Works on GNOME/KDE:**

- Compositor (Mutter/KWin) creates virtual displays
- Compositor reads IMU data from `/dev/shm/breezy_desktop_imu` (shared memory)
- Compositor applies 3D transformations using GLSL shaders
- Compositor renders displays directly to AR glasses display (which is just a regular monitor)
- XRLinuxDriver provides IMU data but doesn't do the rendering

**For X11:**

- X11 doesn't have compositor APIs for 3D rendering
- We need a **standalone 3D rendering application** that:

1. Captures virtual display content (X11 screen capture)
2. Reads IMU data from shared memory
3. Applies 3D transformations using OpenGL/GLSL shaders
4. Renders transformed content to AR glasses display

## Architecture

```
┌────────────────────────────────────────────────────────────┐
│                    Breezy Desktop UI                       │
│              (Python GTK4 - already exists)                │
└──────────────────────┬─────────────────────────────────────┘

                       │
                       │ Creates/manages virtual displays
                       │
        ┌──────────────┴──────────────┐
        │                             │
        ▼                             ▼
┌───────────────┐          ┌─────────────────────────────────┐
│  X11 Backend│          │      3D Renderer App            │
│  (Python)     │          │  (C/C++ or Python + PyOpenGL)   │
│               │          │                                 │
│  - Creates    │          │  ┌────────────────────────────┐ │
│    virtual    │          │  │  Capture Thread (Phase 1)  │ │
│    displays   │─────────▶│  │  (X11 screen capture)      │ │
│    via xrandr │          │  │  - Non-blocking            │ │
│               │          │  │  - May be slower than      │ │
│               │          │  │    glasses refresh rate    │ │
│               │          │  └──────────┬─────────────────┘ │
│               │          │             │ Latest frame      │
│               │          │             ▼                   │
│               │          │  ┌────────────────────────────┐ │
│               │          │  │  3D App API (Phase 2)      │ │
│               │          │  │  (Direct 3D rendering)     │ │
│               │          │  │  - Apps render directly    │ │
│               │          │  │  - Bypass X11 capture      │ │
│               │          │  │  - Register 3D surfaces    │ │
│               │          │  └──────────┬─────────────────┘ │
│               │          │             │                   │
│               │          │             ▼                   │
│               │          │  ┌────────────────────────────┐ │
│               │          │  │  Render Thread             │ │
│               │          │  │  (OpenGL compositor)       │ │
│               │          │  │  - Matches glasses refresh │ │
│               │          │  │  - Composites 2D + 3D      │ │
│               │          │  │  - Reads IMU data          │ │
│               │          │  │  - Applies 3D transforms   │ │
│               │          │  └───────┬────────────────────┘ │
└───────────────┘          └──────────┼──────────────────────┘
                                      │ Renders to AR glasses
                                      │ (at glasses refresh rate)
                                      ▼
                           ┌──────────────────────┐
                           │   XRLinuxDriver      │
                           │  (provides IMU data) │
                           └──────────────────────┘
```

**Critical Performance Requirement:**

- **Frame Rate Synchronization:** Rendering to AR glasses MUST match the glasses' refresh rate (typically 60Hz or 72Hz)
- **Non-Blocking Capture:** X11 screen capture may be slower than the glasses' refresh rate, so it must run in a separate thread
- **Latest Frame Rendering:** The render thread always uses the latest captured frame, never blocking on capture
- **Thread Safety:** Frame buffer must be thread-safe (mutex-protected or lock-free ring buffer)

## Implementation Plan

### Phase 1: Research and Design 3D Rendering Pipeline

**Goal:** Understand the 3D rendering requirements and design the standalone renderer

**Tasks:**

1. Study GNOME/KDE 3D rendering implementation:

- `breezy-desktop/gnome/src/virtualdisplayeffect.js` - GLSL shader code
- `breezy-desktop/gnome/src/math.js` - Math utilities (quaternions, FOV, etc.)
- `breezy-desktop/kwin/src/qml/` - KDE's QtQuick3D implementation

2. Design standalone renderer architecture:

- X11 screen capture method (XShmGetImage, XGetImage, or XComposite)
- OpenGL context creation and management
- GLSL shader porting from GNOME implementation
- IMU data reading from shared memory
- Rendering to AR glasses display (X11 output)

3. Choose implementation language:

- **Option A:** C/C++ with OpenGL (best performance, more complex)
- **Option B:** Python with PyOpenGL (easier to integrate, may be slower)

**Files to examine:**

- `breezy-desktop/gnome/src/virtualdisplayeffect.js` - Shader code
- `breezy-desktop/gnome/src/math.js` - Math utilities
- `breezy-desktop/gnome/src/devicedatastream.js` - IMU data reading
- `breezy-desktop/kwin/src/breezydesktopeffect.cpp` - KDE implementation

### Phase 2: Create X11 Backend Structure

**Goal:** Create `breezy-desktop/x11/` directory structure

**Tasks:**

1. Create directory structure:

- `breezy-desktop/x11/src/` - Backend implementation
- `breezy-desktop/x11/renderer/` - 3D renderer application
- `breezy-desktop/x11/bin/` - Setup/install scripts

2. Create virtual display creation module:

- `breezy-desktop/x11/src/virtualdisplay_x11.py` - Virtual display creation via xrandr
- `breezy-desktop/x11/src/x11_backend.py` - Backend interface

3. Create 3D renderer application:

- `breezy-desktop/x11/renderer/breezy_x11_renderer.c` (or `.py`)
- `breezy-desktop/x11/renderer/shaders/` - GLSL shader files

**Files to create:**

- `breezy-desktop/x11/src/x11_backend.py`
- `breezy-desktop/x11/src/virtualdisplay_x11.py`
- `breezy-desktop/x11/renderer/breezy_x11_renderer.c` (or `.py`)
- `breezy-desktop/x11/renderer/shaders/vertex.glsl`
- `breezy-desktop/x11/renderer/shaders/fragment.glsl`
- `breezy-desktop/x11/bin/breezy_x11_setup`

### Phase 3: Implement Virtual Display Creation

**Goal:** Create virtual displays on X11 using xrandr, with RANDR/X11 as the source of truth

**Tasks:**

1. Implement virtual display creation (initial target: single 3840x2160@60 virtual display):

- Use `cvt 3840 2160 60` to generate a 4K modeline
- Use `xrandr --newmode` and `xrandr --addmode` to attach the mode to a configured dummy output (for example `VIRTUAL1`)
- Use `xrandr --output VIRTUAL1 --mode 3840x2160_60.00` (or equivalent) to activate it

2. Implement display management:

- List virtual displays
- Remove virtual displays
- Handle display lifecycle
- When deciding whether a virtual display is present/active, **query RANDR/X11** (e.g. via `xrandr` or Xlib), never rely solely on any cached JSON state

3. Integrate with renderer:

- Communicate display geometry to renderer
- Handle display creation/destruction events

**Implementation:**

- Use xrandr to create and manage modes on the dummy output (similar to existing `virtualdisplay_x11.py`)
- Store display metadata (width, height, position, ID) in JSON only as auxiliary state (e.g. PIDs and settings), not as the authority on whether a display really exists
- Treat RANDR/X11 as the single source of truth for which virtual displays/modes are active
- Communicate with renderer via IPC (shared memory or D-Bus)

### Phase 4: Implement 3D Renderer Application

**Goal:** Create standalone OpenGL application that renders virtual displays in 3D space, without any synthetic data/stub behaviour.

**Important:** Design the architecture with Phase 2 (direct 3D app rendering) in mind. The renderer should be structured to easily accept both captured 2D frames and direct 3D surfaces in the future.

**Tasks:**

1. Implement multi-threaded architecture:

- **Capture Thread:** Non-blocking X11 screen capture
    - Runs independently of render thread
    - May capture at lower rate than glasses refresh rate
    - Updates shared frame buffer (thread-safe)
- **Render Thread:** Matches glasses refresh rate
    - Always renders at glasses' refresh rate (60Hz/72Hz)
    - Uses latest captured frame (never blocks on capture)
    - Reads IMU data and applies 3D transformations
    - Renders to AR glasses display via OpenGL
- **Frame Buffer Management:**
    - Thread-safe frame buffer (mutex or lock-free ring buffer)
    - Capture thread writes latest frame
    - Render thread reads latest frame (non-blocking)

2. Implement X11 screen capture (Capture Thread):

- Capture virtual display content using X11 APIs (XShmGetImage, XGetImage, or XComposite)
- Convert to OpenGL texture format
- Handle multiple virtual displays
- Update shared frame buffer (thread-safe)

3. Implement IMU data reading (Render Thread):

- Read from `/dev/shm/breezy_desktop_imu` (shared memory)
- Parse data layout (see `breezy-desktop/gnome/src/devicedatastream.js`)
- Extract quaternions, position, timestamps
- Update per-frame (at render rate)

4. Port GLSL shaders and math from GNOME/KDE:

- **Port vertex shader from `virtualdisplayeffect.js`** (lines 489-538) - This is portable GLSL!
    - Quaternion operations (quatConjugate, applyQuaternionToVector)
    - Rotation functions (applyXRotationToVector, applyYRotationToVector)
    - Coordinate system conversions (nwuToESU)
    - Look-ahead prediction calculations
    - FOV and projection matrix handling
- **Port math functions from `math.js`** - Convert JavaScript to C/C++/Python:
    - `diagonalToCrossFOVs()` - FOV calculations
    - `applyQuaternionToVector()` - Quaternion math
    - `fovConversionFns` - FOV conversion for flat/curved displays
    - `normalizeVector()` - Vector normalization
- **Port fragment shader from KWin** (`cursorOverlay.frag`) - Cursor overlay rendering
- **Study KWin's mesh generation** (`CurvableDisplayMesh.qml`) - For curved display support

5. Implement 3D rendering (Render Thread):

- Create OpenGL context (design for future context sharing with 3D apps)
- Set up projection matrix (perspective)
- Apply quaternion transformations based on latest IMU data
- Render latest captured frame to AR glasses display (X11 output)
- Synchronize with glasses refresh rate (VSync)
- **Design compositing pipeline to accept both 2D (captured) and 3D (direct) content** (for Phase 2)

6. Implement display management:

- Handle multiple virtual displays
- Apply individual transformations per display
- Support follow mode (fixed vs. following head)

**Key Implementation Details:**

- **Multi-Threading:** Capture and render must be in separate threads to prevent blocking
- **Frame Rate:** Render thread MUST match glasses refresh rate (60Hz/72Hz), never drop frames
- **Screen Capture:**
- **XShmGetImage (MIT-SHM)** - Recommended for full-screen virtual display capture
    - Faster for visible screen content
    - Lower CPU usage (shared memory)
    - Check availability: `XShmQueryExtension()` or `xdpyinfo | grep -i shm`
    - May block under heavy load (mitigate with separate thread)
- **XComposite extension** - Alternative if needed
    - Can capture individual windows (not needed for our use case - we capture full virtual displays)
    - Accesses compositor's off-screen buffers
    - Slightly higher overhead
    - Check availability: `XCompositeQueryExtension()` or `xdpyinfo | grep -i composite`
    - **Note:** Not needed for Phase 2 (3D apps render directly, no capture)
- **XGetImage** - Fallback (slower, more CPU usage)
- **DRM lease** - Advanced option for direct GPU access (bypasses X11)
    - DRM = Direct Rendering Manager (Linux kernel GPU subsystem)
    - Lowest latency possible
    - More complex to implement
    - **DRM access is available** (check `/dev/dri/` and user's video group membership)
    - **But:** Using DRM directly while X11 is running requires DRM lease (advanced)
    - X11 "owns" the display, so coordination is needed
    - **For Phase 1:** X11 screen capture is simpler and sufficient
    - **For Phase 2:** Direct OpenGL rendering (no DRM needed - apps render directly)
    - **For Future:** DRM lease could be optimization for even lower latency
- **Performance Considerations:**
- xrandr approach adds 1-2 frames latency vs compositor-level
- Use efficient texture uploads (PBOs, direct texture uploads)
- Keep capture thread non-blocking (already planned)
- Expected: 60fps achievable, but with slightly higher latency
- **Frame Buffer:** Use mutex-protected buffer or lock-free ring buffer for thread-safe frame sharing
- **IMU Data:** Read from shared memory file, parse binary layout (see `breezy_desktop.c`)
- **Shaders:** Port from `virtualdisplayeffect.js` - includes quaternion math, FOV calculations, look-ahead prediction
- **Code Reuse:** The GLSL shader code is portable and can be used directly!
- **Math Functions:** Port JavaScript math functions from `math.js` to C/C++/Python
- **VSync:** Enable OpenGL VSync to match glasses refresh rate

**Files to create/modify:**

- `breezy-desktop/x11/renderer/breezy_x11_renderer.c` (or `.py`)
- `breezy-desktop/x11/renderer/capture_thread.c` (or `.py`)
- `breezy-desktop/x11/renderer/render_thread.c` (or `.py`)
- `breezy-desktop/x11/renderer/frame_buffer.c` (or `.py`) - Thread-safe frame buffer
- `breezy-desktop/x11/renderer/shaders/vertex.glsl` - **Port from `virtualdisplayeffect.js` (lines 489-538)**
- `breezy-desktop/x11/renderer/shaders/fragment.glsl` - Port from KWin's `cursorOverlay.frag`
- `breezy-desktop/x11/renderer/math_utils.c` (or `.py`) - **Port math functions from `math.js`**
- `breezy-desktop/x11/renderer/imu_reader.c` (or `.py`)

### Phase 5: Integrate with Breezy Desktop UI

**Goal:** Make X11 backend work with existing breezy-desktop UI

**Tasks:**

1. Update UI to detect X11:

- Already done in `extensionsmanager.py`
- Load X11 backend instead of GNOME/KDE

2. Integrate virtual display management:

- Update `virtualdisplaymanager.py` to use X11 backend
- Start/stop 3D renderer application
- Handle display creation/destruction

3. Test XR driver integration:

- Display distance control
- Follow mode toggle
- Widescreen mode
- Recenter functionality

**Files to modify:**

- `breezy-desktop/ui/src/virtualdisplaymanager.py` - Already partially done
- `breezy-desktop/ui/src/window.py` - Already partially done

### Phase 6: Update Build and Installation

**Goal:** Add X11 support to build system and installation scripts

**Tasks:**

1. Update build system:

- Add 3D renderer to build (C/C++ or Python)
- Handle OpenGL dependencies
- Handle X11 development libraries

2. Create installation script:

- `breezy-desktop/x11/bin/breezy_x11_setup`
- Install dependencies (OpenGL, X11, etc.)
- Build/install renderer application

3. Update documentation:

- Add X11 setup instructions
- Document 3D rendering architecture
- Document limitations/differences

**Files to modify:**

- `breezy-desktop/README.md` - Add X11 section
- `breezy-desktop/x11/README.md` - Document architecture
- Build system files (meson.build, CMakeLists.txt, etc.)

## Key Technical Challenges

**Phase 1 (2D Apps):**

1. **3D Rendering Pipeline:** Creating a standalone OpenGL application that replicates compositor functionality
2. **Frame Rate Synchronization:** Ensuring render thread matches glasses refresh rate (60Hz/72Hz) while capture may be slower
3. **Multi-Threading Architecture:** Implementing non-blocking capture thread that doesn't affect render performance
4. **Thread-Safe Frame Buffer:** Efficient frame buffer sharing between capture and render threads (mutex or lock-free)
5. **Shader Porting:** Porting complex GLSL shaders from GNOME's Cogl to standard OpenGL
6. **X11 Screen Capture:** Efficiently capturing virtual display content (may be slower than render rate)
7. **IMU Data Parsing:** Reading and parsing binary shared memory format
8. **Performance:** Ensuring smooth 60fps rendering with low latency, never dropping frames

**Phase 2 (3D Apps - Future):**

9. **3D Application API:** Design API/protocol for apps to register 3D rendering surfaces
10. **Compositing Architecture:** Efficiently composite 2D (captured) and 3D (direct) content together
11. **OpenGL Context Sharing:** Allow apps to share OpenGL context with renderer for direct rendering
12. **Synchronization:** Coordinate rendering between multiple 3D apps and 2D capture
13. **Resource Management:** Manage GPU resources for both 2D capture and 3D direct rendering

## Success Criteria

**Phase 1 (2D Apps):**

- ✅ Can create virtual displays on X11
- ✅ Virtual displays appear in X11 desktop environment
- ✅ **Virtual displays render in 3D space in AR glasses** (CRITICAL)
- ✅ Follow mode works (display can be fixed or follow head)
- ✅ All XR driver controls work (distance, widescreen, etc.)
- ✅ UI can manage virtual displays (create/remove)
- ✅ Smooth 60fps rendering with low latency (matches glasses refresh rate)
- ✅ Non-blocking screen capture (separate thread, doesn't affect render rate)
- ✅ Frame rate synchronization (render never drops below glasses refresh rate)

**Phase 2 (3D Apps - Future):**

- ✅ Applications can render directly in 3D to AR glasses (bypass X11 capture)
- ✅ 2D and 3D content can be composited together on the same desktop
- ✅ API/protocol for apps to register 3D rendering surfaces
- ✅ Multiple 3D apps can render simultaneously
- ✅ Performance remains smooth with mixed 2D/3D content

## Implementation Language Decision

**Recommendation:** Start with **Python + PyOpenGL** for faster development and easier integration, then optimize to C/C++ if performance is insufficient.

**Python Advantages:**

- Easier integration with existing Python codebase
- Faster development iteration
- Easier to debug and test
- Can use existing Python libraries (numpy for math, etc.)

**C/C++ Advantages:**

- Better performance (critical for 60fps rendering)
- Lower latency
- More control over memory management

**Hybrid Approach:**

- Use Python for backend and UI integration
- Use C/C++ for the 3D renderer (compiled as separate binary)
- Communicate via IPC (shared memory or D-Bus)

## Future Architecture: Direct 3D Application Rendering (Phase 2)

**Goal:** Allow applications to render directly in 3D to AR glasses, bypassing X11 screen capture. This enables true 3D desktop applications alongside traditional 2D applications.

**Design Considerations:**

1. **API/Protocol Design:**

- D-Bus interface or shared memory protocol for apps to register 3D surfaces
- Apps provide OpenGL textures or framebuffers directly
- Apps specify 3D position, rotation, scale in world space
- Apps can request IMU data for head tracking
- Apps can query available rendering capabilities
- **Important:** 3D apps do NOT use screen capture - they render directly via OpenGL!

2. **Compositing Architecture:**

- Renderer maintains list of 2D (captured) and 3D (direct) content
- Composite all content in single OpenGL render pass
- Apply 3D transformations to both 2D and 3D content
- Handle depth sorting and z-ordering
- Support transparency and blending

3. **OpenGL Context Sharing:**

- Renderer provides shared OpenGL context for apps
- Apps can create textures/framebuffers in shared context
- Efficient texture sharing without copy operations
- Support for multiple apps sharing the same context

4. **Synchronization:**

- Apps render at their own rate (may be different from glasses refresh)
- Renderer always uses latest content from each app
- Similar to capture thread: non-blocking, use latest frame
- Apps can request frame synchronization if needed

5. **Resource Management:**

- Track GPU memory usage for both 2D capture and 3D direct rendering
- Handle app lifecycle (start/stop 3D rendering)
- Clean up resources when apps disconnect
- Handle app crashes gracefully

6. **Architecture Extensibility:**

- Design Phase 1 renderer with Phase 2 in mind
- Separate compositing logic from capture logic
- Make renderer accept both captured frames and direct 3D surfaces
- Design API early (even if not implemented) to guide architecture

**Implementation Approach:**

- Design API first (D-Bus or shared memory protocol) before implementing
- Extend renderer to support both 2D capture and 3D direct rendering
- Create example 3D application to demonstrate API
- Document API for third-party developers
- Consider backward compatibility with Phase 1 (2D capture)

**Benefits:**

- Lower latency for 3D apps (no X11 capture overhead)
- Better performance (direct GPU rendering)
- More immersive 3D experiences
- Enables true 3D desktop applications
- Allows mixed 2D/3D desktop environments

**Note:** Phase 1 (2D apps via X11 capture) remains available for applications that don't need 3D rendering or prefer the simpler approach. Both modes can coexist on the same desktop.

## Architecture Clarification: Compositor vs. Specialized Renderer

**Important Distinction:** We are NOT building a full compositor replacement. We're building a specialized 3D renderer that works alongside X11's existing compositor.

### X11's Existing Compositors

- **X11-based desktops have compositors** - For example, XFCE uses Xfwm4 (XFCE Window Manager), and other desktops have their own compositors
- It handles normal desktop compositing (shadows, transparency, window effects)
- It's lightweight and designed for traditional 2D desktop use
- It does NOT have:
- Advanced 3D rendering capabilities
- Plugin/extension APIs like Mutter/KWin
- Virtual display creation APIs
- Direct integration with AR glasses rendering

### What We're Building

- **Specialized 3D Renderer** (not a full compositor)
- Works **ALONGSIDE** the desktop compositor, not replacing it
- The desktop compositor continues to handle normal desktop compositing
- Our renderer only handles:
- Virtual display creation (via xrandr)
- 3D transformation of captured content
- Rendering to AR glasses display
- Future: Direct 3D app rendering

### Why Not Base It on Mutter or KWin?

**Mutter (GNOME):**

- Deeply integrated with GNOME Shell
- Not just a compositor - it's the window manager, compositor, and shell all in one
- Tightly coupled to GNOME's architecture (GObject, Clutter, etc.)
- Extracting just the rendering parts would require:
- Rewriting large portions to remove GNOME dependencies
- Essentially rebuilding it from scratch anyway

**KWin (KDE):**

- Integrated with KDE's Qt-based architecture
- Tightly coupled to KDE's window management system
- Uses QtQuick3D for 3D rendering (Qt-specific)
- Similar issues to Mutter - too tightly integrated

**Our Approach:**

- Build a specialized, lightweight renderer from scratch
- Focused only on AR glasses rendering (not full desktop compositing)
- Can be simpler and more maintainable than extracting from Mutter/KWin
- Designed specifically for X11's architecture

### The "Foolish" Question: Should We Just Use GNOME?

**Arguments FOR using GNOME:**

- ✅ Breezy Desktop already works on GNOME
- ✅ Mutter already has all the compositor capabilities
- ✅ Less development work
- ✅ More stable (already tested)

**Arguments FOR building for X11:**

- ✅ You prefer X11's workflow/UI/philosophy
- ✅ X11 is lighter weight
- ✅ More control over the implementation
- ✅ Specialized solution can be optimized for AR glasses use case
- ✅ Not tied to GNOME's architecture decisions

**The Reality:**

- If you're happy with GNOME, use GNOME - it's the path of least resistance
- If you prefer X11, building a specialized renderer is reasonable
- The specialized renderer can be simpler than a full compositor
