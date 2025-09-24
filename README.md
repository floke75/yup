# Rive → NDI Offscreen Rendering Pipeline

## Project Overview
This repository delivers a Windows-first workflow that renders [Rive](https://rive.app/) animations
headlessly with Direct3D 11 and publishes the resulting BGRA frames over Network Device Interface
(NDI). The project began as a fork of the broader YUP multimedia framework, but it now focuses
exclusively on GPU rendering, Python bindings, and video-over-IP delivery. Audio engines, plugin
hosts, and legacy application layers remain disabled so the codebase concentrates on the
Rive → NDI path.

### Key Capabilities
- **Offscreen Direct3D 11 renderer:** `yup::RiveOffscreenRenderer` initialises a swapchain-free D3D11
device, renders artboards into BGRA textures, and provides deterministic CPU readback.
- **Python bindings with zero-copy access:** The `yup_rive_renderer` extension exposes artboard
enumeration, animation/state-machine control, and both copy and shared-memory views of the frame
buffer.
- **Python NDI orchestration:** The `yup_ndi` package coordinates multiple renderer instances,
forwards BGRA frames to `cyndilib` senders, and offers control hooks for pause/resume, animation
switching, and metadata updates.
- **Tested workflow:** GoogleTest and `pytest` suites validate renderer semantics, binding behaviour,
and orchestrator integration without requiring native NDI libraries during development.

## Repository Layout
| Path | Description |
| ---- | ----------- |
| `modules/yup_gui/artboard/` | Direct3D 11 offscreen renderer and Rive artboard helpers. |
| `modules/yup_graphics/` | GPU utilities reused by the renderer implementation. |
| `modules/yup_core/`, `modules/yup_events/` | Logging, timing, and COM helpers shared across modules. |
| `python/src/yup_rive_renderer.cpp` | pybind11 bindings for `RiveOffscreenRenderer`. |
| `python/yup_ndi/` | Python orchestrator that drives renderers and NDI senders. |
| `python/tests/` | Unit tests covering the bindings and orchestrator APIs. |
| `docs/` | Architecture notes, build guides, and pipeline walkthroughs. |
| `tools/`, `justfile` | Helper scripts and automation recipes for Windows builds and smoke tests. |

## Getting Started
### Prerequisites
- Windows 10/11 with Visual Studio 2022 and the Windows SDK installed.
- Python 3.10+ with `pip`, `cmake`, and `ninja` available.
- (Optional) [NDI Tools](https://ndi.video/tools/) or another NDI receiver for end-to-end testing.
- (Optional) `cyndilib>=0.0.8` when you intend to publish live NDI streams.

### Clone the Repository
```powershell
git clone https://github.com/kunitoki/yup.git
cd yup
```

### Configure & Build Native Targets (Visual Studio)
Generate a project that focuses on the renderer, bindings, and tests:
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
  -DYUP_ENABLE_AUDIO_MODULES=OFF \
  -DYUP_BUILD_TESTS=ON \
  -DYUP_BUILD_EXAMPLES=OFF
cmake --build build --config Release
```
Disabling the audio modules keeps legacy subsystems out of the build while the tests remain
available.

### Build Python Wheels
Use the provided `just` recipe (or run the underlying commands manually) to build and test the
Python packages:
```powershell
just python_wheel
```
The recipe invokes `python -m build --wheel`, reinstalls the freshly built wheel, and executes the
Python unit tests. To skip installation and testing, run `python -m build --wheel` from the `python`
directory instead.

> **Note:** The legacy `yup` core/events/graphics bindings still require the compiled native module.
> The corresponding test suites now skip automatically when the extension is unavailable so that
> documentation-only environments can execute the remaining Python checks.

### Run Renderer ↔ NDI Smoke Tests
Targeted smoke tests verify zero-copy frame access and orchestrator behaviour:
```powershell
just python_smoke
```
The tests run without native GPU or NDI dependencies thanks to the bundled fakes.

### Publish a Rive Stream over NDI
Install the built wheel into your Python environment alongside `cyndilib`, then orchestrate a stream
with the high-level API:
```python
from yup_ndi import NDIOrchestrator, NDIStreamConfig

with NDIOrchestrator() as orchestrator:
    orchestrator.add_stream(
        NDIStreamConfig(
            name="RiveNDI",
            width=1920,
            height=1080,
            riv_path="assets/demo.riv",
            animation="Intro",
            loop_animation=True,
            ndi_groups="Studio",
        )
    )

    # Drive the renderer at ~60 FPS.
    while True:
        orchestrator.advance_all(1 / 60)
```
Use `apply_stream_control` to adjust playback at runtime (pause, resume, change animations, or tweak
state-machine inputs).

> `NDIStreamConfig.frame_rate` mirrors the CLI semantics: provide a positive float/Fraction (or a
> `(numerator, denominator)` tuple) to lock the stream to a deterministic cadence. Supply `0` or `None`
> to follow real time. Invalid tuples raise explicit errors so configuration mistakes are caught early.

## Development Guidelines
- Follow Allman brace style and the conventions outlined in `CLAUDE.md`.
- Keep renderer code modular and favour RAII for GPU resources.
- Mirror API changes across the C++ renderer, the pybind11 bindings, and the Python orchestrator in a
single change.
- Extend GoogleTests or `pytest` suites when modifying behaviour.
- Document new public APIs with Doxygen (C++) or docstrings (Python).
- Use `just --list` to discover additional automation recipes.

## Roadmap
- ✅ Direct3D 11 offscreen rendering and BGRA readback.
- ✅ Rive animation playback with deterministic timing.
- ✅ Python NDI streaming orchestration with multi-sender support.
- 🚧 Automated packaging pipeline for distributing signed Windows wheels/installers.
- 🚧 Telemetry hooks for measuring frame latency and GPU utilisation.

## Contributing
Pull requests are welcome! Focus contributions on the renderer, animation control layer, Python
bindings, or NDI tooling. Open an issue to discuss larger architectural changes before
implementation. Tests and documentation updates are expected alongside feature work.

## License
Distributed under the ISC License. See [`LICENSE`](./LICENSE) for details.
