# Rive → NDI Pipeline Overview (Windows)

This guide documents the Windows-focused workflow for turning Rive animations into NDI video streams using YUP.
It highlights the current code locations, the components that are still landing, and how they are expected to fit together.

## Goals For The Windows Pipeline
- **Headless Direct3D 11 Rendering:** `yup::RiveOffscreenRenderer` initialises a swapchain-free D3D11 device, renders artboards into BGRA textures, and provides deterministic CPU readback of each frame.
- **Python-Friendly Surface:** The `yup_rive_renderer` pybind11 module exposes animation loading/control and frame retrieval APIs tailored for automation agents.
- **NDI Transmission:** The `yup_ndi` orchestration layer subscribes to renderer frames, maps timestamps, and forwards BGRA buffers to `cyndilib.Sender` instances.
- **Minimal Footprint:** Builds stay focused on the renderer/NDI stack by disabling unrelated audio and plugin systems, keeping iteration times tight on Windows.

## Key Source Locations
| Component | Purpose | Location |
| --- | --- | --- |
| Offscreen renderer core | Implements Direct3D 11 setup, Rive artboard rendering, and frame readback. | `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.h/.cpp` |
| Renderer unit tests | Validates stride, dimensions, pause semantics, and shared-buffer behaviour. | `tests/yup_gui/yup_RiveOffscreenRenderer.cpp` |
| Python binding layer | Provides `pybind11` bindings for loading `.riv` files, advancing animations, and fetching BGRA frames. | `python/src/yup_rive_renderer.cpp`, `python/CMakeLists.txt` |
| NDI sender wrapper | Bridges Python frames into NDI streams, coordinating lifecycle and multi-stream publishing. | `python/yup_ndi/orchestrator.py`, `python/yup_ndi/__init__.py` |
| Python smoke tests | Exercises binding import/zero-copy views and mocked NDI sends. | `python/tests/test_yup_rive_renderer/`, `python/tests/test_yup_ndi/` |

## How The Pieces Fit Together
1. **Render Frames:** `yup::RiveOffscreenRenderer` manages the Direct3D 11 device, renders the requested artboard/animation into an offscreen texture, and exposes CPU-readable BGRA data.
2. **Expose To Python:** The pybind11 module loads `.riv` assets, drives animations (state machines or timelines), and exposes each rendered frame as a Python-accessible buffer or NumPy array.
3. **Publish Over NDI:** The forthcoming NDI wrapper subscribes to frames from the binding layer, converts timing metadata, and pushes them to `cyndilib`'s `Sender`, enabling multi-instance streaming. Optional REST/OSC endpoints can sit above this layer for remote control.

The intent is to keep the renderer performant and deterministic while letting Python orchestrate scheduling, IO, and integrations.

## Recommended Build Configuration (Windows)
Use the focused set of CMake options below when configuring Visual Studio builds:

```bash
cmake -S . -B build-rive-ndi-win \
  -DYUP_ENABLE_AUDIO_MODULES=OFF \
  -DYUP_ENABLE_PLUGIN_MODULES=OFF \
  -DYUP_ENABLE_EXAMPLES=OFF
```

These flags trim unrelated subsystems and speed up compilation while you iterate on the renderer, bindings, and NDI layer. Additional feature toggles can stay at their defaults unless you need them for debugging.

### Audio Module Toggle

The renderer/NDI stack stays leanest when the legacy audio toolchain is excluded. Set `YUP_ENABLE_AUDIO_MODULES=OFF` at configure time to keep the build focused on graphics and Python:

```bash
cmake -S . -B build-rive-ndi-win \
  -DYUP_ENABLE_AUDIO_MODULES=OFF
```

Python wheel builds respect the same switch via an environment variable. When creating artifacts for automation hosts, export the variable before invoking `pip` or `python -m build`:

```bash
set YUP_ENABLE_AUDIO_MODULES=0
python -m build python
```

Use `1` to re-enable the audio modules if a downstream integration actually needs them.

## Streamlined Commands
Use the following helpers while iterating on Windows:

```powershell
cmake -S . -B build/rive-ndi-win `
  -G "Visual Studio 17 2022" `
  -DYUP_ENABLE_AUDIO_MODULES=OFF `
  -DYUP_ENABLE_PLUGIN_MODULES=OFF `
  -DYUP_ENABLE_EXAMPLES=OFF

cmake --build build/rive-ndi-win --target yup_gui --config RelWithDebInfo

cmake -S python -B build/rive-ndi-win-python `
  -G "Visual Studio 17 2022" `
  -DYUP_ENABLE_AUDIO_MODULES=OFF

cmake --build build/rive-ndi-win-python --target yup_rive_renderer --config RelWithDebInfo
cmake --build build/rive-ndi-win-python --target yup --config RelWithDebInfo

just python_test_rive_ndi
```

The `just python_test_rive_ndi` recipe executes the focused Python suites so you can validate binding imports, zero-copy frame acquisition, and mocked NDI sends without running the full legacy test matrix.

## Next Steps For Contributors
- Profile end-to-end frame throughput on Windows hardware and tune staging-buffer reuse if needed.
- Expand documentation with troubleshooting notes for common MSVC/NDI configuration hiccups.
- Layer integration tests that combine the pybind11 renderer with live NDI sends on target machines.
