# Rive → NDI Pipeline Overview (Windows)

This guide documents the Windows-focused workflow for turning Rive animations into NDI video streams using YUP.
It highlights the current code locations, the components that are still landing, and how they are expected to fit together.

## Goals For The Windows Pipeline
- **Headless Direct3D 11 Rendering:** Extend `yup::RiveOffscreenRenderer` so it can initialise a swapchain-free D3D11 device, render artboards into BGRA textures, and provide deterministic CPU readback of each frame.
- **Python-Friendly Surface:** Shape a pybind11 layer (planned in `python/`) that exposes animation loading/control and frame retrieval APIs tailored for automation agents.
- **NDI Transmission:** Wrap the renderer output with a lightweight Python control layer that forwards BGRA frames to NDI via the forthcoming wrapper module.
- **Minimal Footprint:** Focus builds on the renderer/NDI stack by disabling unrelated audio and plugin systems, keeping iteration times tight on Windows.

## Key Source Locations
| Component | Purpose | Current / Planned Location |
| --- | --- | --- |
| Offscreen renderer core | Implements Direct3D 11 setup, Rive artboard rendering, and frame readback. | `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.h/.cpp` |
| Python binding layer | Provides `pybind11` bindings for loading `.riv` files, advancing animations, and fetching BGRA frames. | `python/` (new module: _TBD name_) |
| NDI sender wrapper | Bridges Python frames into NDI streams, coordinating lifecycle and multi-stream publishing. | `python/` (new wrapper package planned) |

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

## Streamlined Commands
A `just rive_ndi_win` recipe (landing soon in the project `justfile`) will encapsulate the configuration and build steps above, plus invoke targeted tests for the renderer/binding/NDI stack. Once available, run:

```bash
just rive_ndi_win
```

Until the recipe is merged, execute the `cmake` configuration manually and build the `yup_gui` targets inside Visual Studio.

## Next Steps For Contributors
- Finish wiring Direct3D 11 BGRA readback within `RiveOffscreenRenderer`.
- Implement the pybind11 module that surfaces renderer controls to Python.
- Integrate the NDI sender wrapper and add smoke tests that validate frame emission.
- Update documentation as the Python module and `just` recipe go live so downstream agents inherit the latest workflow.
