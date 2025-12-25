# Documentation Consolidation Plan

## Current Document Structure

### Main Document
- **`BREEZY_X11_TECHNICAL.md`**: Main technical documentation (should be the primary reference)

### Detailed Design Documents (Referenced from Main)
- **`XORG_VIRTUAL_XR_API.md`**: Detailed API design, data structures, code examples
- **`XORG_IMPLEMENTATION_STATUS.md`**: Implementation status tracking and remaining tasks

### Implementation-Specific Docs (Keep Separate)
- **`x11/renderer/IMPLEMENTATION_STATUS.md`**: Renderer implementation status
- **`x11/renderer/TESTING_GUIDE.md`**: Testing procedures for X11 renderer
- **`x11/renderer/DMA_BUF_OPTIMIZATION.md`**: DMA-BUF optimization details
- **`x11/renderer/PIPEWIRE_VS_DRM.md`**: Performance comparison

## Duplication Analysis

### Overlap Between Documents

1. **BREEZY_X11_TECHNICAL.md Section 7.2** vs **XORG_VIRTUAL_XR_API.md**:
   - **Overlap**: Both describe virtual XR outputs architecture
   - **Resolution**: BREEZY_X11_TECHNICAL should provide high-level overview, reference XORG_VIRTUAL_XR_API for details
   - **Status**: ✅ Updated - BREEZY_X11_TECHNICAL now references XORG_VIRTUAL_XR_API for detailed design

2. **BREEZY_X11_TECHNICAL.md Section 7** vs **XORG_IMPLEMENTATION_STATUS.md**:
   - **Overlap**: Both list implementation status
   - **Resolution**: BREEZY_X11_TECHNICAL should have high-level status, XORG_IMPLEMENTATION_STATUS has detailed task tracking
   - **Status**: ✅ Updated - BREEZY_X11_TECHNICAL now references XORG_IMPLEMENTATION_STATUS

3. **BREEZY_X11_TECHNICAL.md** vs **x11/renderer/IMPLEMENTATION_STATUS.md**:
   - **Overlap**: Minimal - different scopes (Xorg driver vs renderer)
   - **Resolution**: Keep separate (renderer-specific vs driver-specific)

## Recommended Structure

```
BREEZY_X11_TECHNICAL.md (Main Document)
├── High-level overview of all components
├── References to detailed docs for:
│   ├── X11/Xorg virtual connector design → XORG_VIRTUAL_XR_API.md
│   ├── Implementation status → XORG_IMPLEMENTATION_STATUS.md
│   └── Renderer details → x11/renderer/IMPLEMENTATION_STATUS.md
└── Language choice rationale (Python vs C)

XORG_VIRTUAL_XR_API.md (Detailed Design)
├── API design details
├── Data structures
├── Code examples
└── Communication interface specs

XORG_IMPLEMENTATION_STATUS.md (Status Tracking)
├── Completed items
├── Remaining tasks
└── Testing requirements
```

## Language Choice: Python vs C

### Current Architecture

**Performance-Critical Components (C/C++)**:
- **3D Renderer** (`x11/renderer/breezy_x11_renderer.c`): C-based for maximum performance
- **KWin Effect** (`kwin/src/breezydesktopeffect.cpp`): C++ for compositor integration

**Orchestration/IPC Components (Python/JavaScript)**:
- **X11 Backend** (`x11/src/x11_backend.py`): Python for xrandr orchestration
- **KWin IPC** (`kwin/src/xrdriveripc/xrdriveripc.py`): Python for XRLinuxDriver IPC
- **GNOME Extension** (`gnome/src/extension.js`): JavaScript (required by GNOME)

### Rationale

**Python is appropriate for**:
- **xrandr orchestration**:** Simple subprocess calls, no performance impact
- **IPC with XRLinuxDriver**: JSON parsing, shared memory access - not performance-critical
- **State management**: Configuration, calibration detection - not in hot path
- **Rapid development**: Easier to iterate on backend logic

**C/C++ is required for**:
- **3D rendering**: OpenGL calls, shader execution, frame processing - performance-critical
- **Compositor integration**: Must match compositor's language (C++ for KWin, JavaScript for GNOME)

**Conclusion**: The current mixed-language approach is optimal:
- Backend orchestration in Python (matches KWin's pattern)
- Performance-critical rendering in C (matches industry standard)
- This separation of concerns is common in graphics applications

