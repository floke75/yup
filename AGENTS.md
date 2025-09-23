# Coding Agent Guide: GPU-Accelerated Rive Renderer with NDI Output

## Mission Overview
You are extending YUP to deliver a Windows-focused pipeline that renders Rive (`.riv`) animations offscreen via Direct3D 11 and exposes frames (with alpha) to Python for NDI transmission. The end result is a reusable C++ core (built with CMake/Visual Studio), a pybind11-powered Python module, and a lightweight Python control layer that can drive NDI streams and optional REST/OSC endpoints. Audio/plugin subsystems in YUP remain untouched and disabled at runtime.

**Supported platform:** Windows 11 with MSVC 2022 and the Windows 10/11 SDK. Other platforms may compile stub implementations, but only Windows requires full functionality.

## Implementation Snapshot (April 2025)
| Area | Status | Key Files |
| --- | --- | --- |
| Direct3D 11 offscreen renderer | ✅ `yup::RiveOffscreenRenderer` initialises the D3D11 device/context, renders into a BGRA texture, and performs CPU readback into a shared buffer. | `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.h/.cpp` |
| Scene & animation control | ✅ Artboard enumeration, animation/state-machine playback, pause toggling, and state-machine input helpers are exposed through the renderer API. | `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.cpp` |
| Renderer tests | ✅ GoogleTest coverage validates frame stride/dimensions, pause semantics, shared-buffer behaviour, and artboard switching. | `tests/yup_gui/yup_RiveOffscreenRenderer.cpp` |
| Python renderer binding | ✅ `yup_rive_renderer` pybind11 module wraps renderer construction, artboard/animation APIs, and exposes zero-copy frame views. | `python/src/yup_rive_renderer.cpp`, build glue in `python/CMakeLists.txt`, packaging metadata in `python/pyproject.toml` |
| Python NDI orchestration | ✅ `yup_ndi` package manages multi-stream orchestration, timestamp mapping, metadata dispatch, and optional control hooks using mocked `cyndilib` senders for tests. | `python/yup_ndi/__init__.py`, `python/yup_ndi/orchestrator.py`, tests under `python/tests/test_yup_ndi/` |
| Python test harness | ✅ `pytest` suites cover binding behaviour, orchestrator frame flow, and provide fake renderer/sender utilities that avoid native DLL requirements. | `python/tests/` |
| Documentation & developer workflow | ⚠️ Needs expansion in `docs/` and `tools/` to describe Windows build steps, wheel packaging, and orchestration usage. |

## Key Components & File Map
- **Renderer core:** `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.h/.cpp`
  - D3D11 setup, staging texture readback, artboard/animation/state-machine management, pause controls, and frame-buffer accessors.
  - Non-Windows fallback stub maintains API compatibility but reports unsupported status.
- **Supporting tests:** `tests/yup_gui/yup_RiveOffscreenRenderer.cpp`
  - Exercises pause toggling, buffer sharing, stride validation, and artboard selection across stubbed renderers.
- **Python binding:** `python/src/yup_rive_renderer.cpp`
  - Binds renderer lifecycle, exposes artboard lists, playback controls, state-machine inputs, and `memoryview`-friendly BGRA buffers.
  - Build definitions live in `python/CMakeLists.txt`; packaging metadata in `python/pyproject.toml` targets MSVC 2022 + C++17 with Ninja.
- **Python orchestration:** `python/yup_ndi/orchestrator.py`
  - Implements `RiveStreamOrchestrator`, stream configuration dataclasses, metadata callbacks, REST/OSC hook placeholders, and stream lifecycle management.
  - Package entry point re-exports live in `python/yup_ndi/__init__.py`.
- **Python tests & utilities:**
  - `python/tests/test_yup_ndi/test_orchestrator.py` supplies orchestrator coverage with fake renderer/sender fixtures.
  - `python/tests/common.py`, `python/tests/utilities.py`, and `python/tests/conftest.py` provide shared helpers and dependency guards when the native extension is unavailable.
- **Packaging scaffold:** `python/setup.py`, `python/MANIFEST.in`, and `python/tools/` for Windows wheel generation (needs auditing as work continues).

## Current Priorities
1. **Documentation & developer tooling**
   - Expand `docs/` with Windows 11 build instructions for the renderer, Python binding, and NDI orchestrator usage.
   - Update `justfile` or add scripts in `tools/` describing end-to-end workflows (configure VS environment, build wheel, run tests).
2. **Integration testing**
   - Plan combined smoke tests that exercise the pybind11 module feeding the orchestrator (mocks acceptable for automation; document manual NDI validation).
3. **Performance validation**
   - Profile frame throughput and ensure zero-copy semantics across the renderer → Python boundary when running on Windows hardware.

## Workflow Expectations
- **Analyse before coding:** survey existing helpers (e.g., `modules/yup_graphics/`, `modules/yup_core/`) before introducing new abstractions; reuse utilities whenever possible.
- **Code style:** adhere to Allman braces, JUCE-style naming, and include-order conventions outlined in `CLAUDE.md`. Add Doxygen for new public APIs.
- **Error handling:** use RAII for graphics resources, propagate detailed errors through `RiveOffscreenRenderer::getLastError()` and mirror them in Python exceptions.
- **Testing:** accompany new functionality with deterministic GoogleTests or `pytest` cases. Provide mocks when native dependencies (NDI, GPU) are unavailable in CI.
- **Windows focus:** keep build flags, documentation, and scripts aligned with Windows 11 + MSVC 2022; non-Windows paths should remain stubs without extra investment.
- **Change scope:** keep commits targeted—avoid formatting-only edits and unrelated subsystem changes.

Stay focused on delivering a polished Windows pipeline from Rive rendering through Python-based NDI streaming while maintaining clear documentation for future contributors.
