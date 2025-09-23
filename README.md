# Rive → NDI Offscreen Rendering Pipeline

## Project Overview
This repository hosts a greenfield effort to deliver a Windows-first pipeline that renders [Rive](https://rive.app/) animations offscreen through Direct3D 11 and streams the resulting BGRA frames via Network Device Interface (NDI). Although the codebase originated as a fork of the YUP multimedia framework, this project now evolves independently with a focused scope: rock-solid GPU rendering, ergonomic Python bindings, and reliable video-over-IP delivery. No audio engines, plugin systems, or legacy YUP application layers are maintained here—only the components required to move pixels from `.riv` files to NDI receivers.

### Guiding Principles
- **Purpose-built:** Every subsystem exists to support headless Rive playback and NDI streaming; unrelated YUP functionality has been removed or frozen.
- **Windows-centric:** Direct3D 11 is the primary renderer. Other platforms may build, but they are not release targets.
- **Python-first orchestration:** A pybind11 module exposes rendering and animation control to Python so operators can script playback, scheduling, and stream publication.
- **Transparent licensing:** All code remains under the permissive ISC license, preserving the original openness of the YUP fork.

## Core Objectives
1. **Offscreen Renderer:** Initialise a Direct3D 11 device without a swap chain, render Rive artboards into GPU textures, and provide fast CPU readback of BGRA frames.
2. **Rive Animation Control:** Load `.riv` assets, select artboards, drive animations/state machines, and keep frame timing deterministic.
3. **Python Bindings:** Ship a `yup_rive_renderer` extension with ergonomic APIs for loading animations, advancing frames, and retrieving pixel buffers.
4. **NDI Streaming Layer:** Implement a Python control surface that ingests BGRA frames and publishes them through [ndi-python](https://github.com/Palakis/obs-ndi) or equivalent bindings, supporting multiple concurrent senders.
5. **Operational Tooling:** Provide scripts, examples, and documentation for configuring Windows builds (MSVC 2022, Windows 10/11 SDK) and packaging Python wheels.

## Repository Layout
| Path | Description |
| ---- | ----------- |
| `modules/yup_gui/artboard/` | Direct3D 11 offscreen renderer and helpers for Rive artboard management. |
| `modules/yup_graphics/` | GPU utility classes (textures, command contexts, synchronization). |
| `modules/yup_core/` & `modules/yup_events/` | Logging, threading, and timing utilities reused by the renderer. |
| `python/` | pybind11 extension sources, packaging metadata, and unit tests. |
| `examples/render/` | Sample applications demonstrating headless playback and frame extraction. |
| `docs/` | Architectural notes (e.g., `rive_ndi_overview.md`), build guides, and troubleshooting tips. |
| `tools/` | Build, packaging, and deployment helpers. |

## Getting Started
### Prerequisites
- Windows 10/11 with Visual Studio 2022 and the Windows SDK.
- Python 3.10+ with `pip` and a working C++ build toolchain.
- (Optional) Access to an NDI receiver for end-to-end validation.

### Clone the Repository
```bash
git clone https://github.com/kunitoki/yup.git
cd yup
```

### Configure & Build (Windows/MSVC)
```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
  -DYUP_BUILD_PYTHON=ON \
  -DYUP_ENABLE_RIVE_RENDERER=ON
cmake --build build --config Release
```

### Build Python Wheels
```powershell
cd python
pip install -r requirements.txt
pip wheel . --wheel-dir dist
```
Install the resulting wheel into your Python environment and import `yup_rive_renderer` to access the bindings.

### Run Render → NDI Example
1. Install the Python wheel as described above.
2. Ensure the [NDI Tools](https://ndi.video/tools/) runtime is installed.
3. Execute the streaming demo:
   ```powershell
   python examples/python/stream_rive_to_ndi.py --riv assets/demo.riv --sender-name "RiveNDI"
   ```
4. Confirm the stream appears inside **NDI Studio Monitor** or your preferred receiver.

## Development Guidelines
- Follow Allman brace style and the conventions outlined in `CLAUDE.md`.
- Keep renderer code modular and prefer RAII for GPU resources.
- Add or update unit tests under `python/tests/` or C++ test suites when changing behaviour.
- Document new public APIs with Doxygen (C++) or doctrings (Python).
- Use `just` recipes or CMake presets to generate platform-specific projects; see `just --list` for details.

### Preparing for Redundancy Pruning
The next major milestone is a deliberate pruning pass that eliminates any lingering systems that do not feed the Rive → NDI data
path. To keep that effort safe and predictable:

- **Map dependencies before deleting files.** Renderer sources still depend on a curated subset of `yup_core`, `yup_events`, an
  d `yup_graphics`; validate includes before removing whole modules.
- **Preserve API contracts.** The Python extension and orchestrator expect specific method names (see `yup_rive_renderer` bindi
  ngs and `yup_ndi` package). If an internal helper is renamed or removed, mirror the change across bindings/tests in the same b
  ranch.
- **Annotate temporary shims.** Files that only exist to bridge from legacy YUP types into the renderer should gain comments exp
  laining their transitional role so they can be confidently excised once replacement utilities land.
- **Keep tests authoritative.** When deleting redundant code, prefer expanding the renderer/orchestrator unit tests instead of a
  dding new mocks. The pruning pass should end with fewer, more focused tests that still validate frame delivery.
- **Avoid platform regressions.** Even though Windows is the release target, stub implementations for macOS/Linux keep our CI fl
  owing. Replace them only when an equivalent stub is available.

## Roadmap
- ✅ Direct3D 11 offscreen rendering prototype.
- ✅ Rive animation playback with deterministic timing.
- 🚧 Python NDI streaming orchestration with multi-sender support.
- 🚧 Automated build & packaging pipeline for wheels and Windows installers.
- 🚧 Telemetry hooks for measuring frame latency and CPU/GPU utilisation.

## Contributing
Pull requests are welcome! Please focus contributions on the renderer, animation control layer, Python bindings, or NDI tooling. Open an issue to discuss larger architectural changes before implementation. Tests and documentation updates are expected alongside feature work.

## License
Distributed under the ISC License. See [`LICENSE`](./LICENSE) for details.
