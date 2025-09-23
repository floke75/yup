# Coding Agent Guide: GPU-Accelerated Rive Renderer with NDI Output

## Mission Overview
You are extending YUP to deliver a Windows-focused pipeline that renders Rive (`.riv`) animations offscreen via Direct3D 11 and exposes frames (with alpha) to Python for NDI transmission. The end result is a reusable C++ core (built with CMake/Visual Studio), a pybind11-powered Python module, and a lightweight Python control layer that can drive NDI streams and optional REST/OSC endpoints. Audio/plugin subsystems in YUP remain untouched and disabled at runtime.

**Supported platform:** Windows 11 with MSVC 2022 and the Windows 10/11 SDK. Other platforms can compile stubs, but only Windows requires full functionality.

## Current Implementation Snapshot (April 2025)
| Area | Status | Key Files |
| --- | --- | --- |
| Offscreen renderer shell | ✅ `yup::RiveOffscreenRenderer` exposes construction, `.riv` loading, artboard/animation/state-machine enumeration, active-artboard selection, pause control, frame accessors, and error reporting. | `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.h/.cpp`
| Renderer tests | ✅ GoogleTest suite covers frame stride/dimensions, shared-buffer semantics, pause toggling, and artboard selection logic. | `tests/yup_gui/yup_RiveOffscreenRenderer.cpp`
| Direct3D 11 backend | ⚠️ Still incomplete—stubbed paths remain; GPU device creation, BGRA target setup, and staging-texture readback need to be finalised. | `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.cpp`
| Animation/state-machine ticking | ⚠️ Needs deterministic advancement logic tied to the renderer loop and exposed inputs. | `modules/yup_gui/artboard/` (renderer implementation), potential helpers under `modules/yup_gui/component/`
| Python bindings | ⏳ Not started. Plan to add under `python/src/` with build glue in `python/CMakeLists.txt` and `python/pyproject.toml`.
| Python NDI layer | ⏳ Not started. Target package directory `python/yup_ndi/` with tests in `python/tests/` and optional tooling in `tools/`.
| Documentation | ⏳ Needs expansion in `docs/` to describe Windows workflow, renderer usage, Python module, and NDI orchestration.

## Near-Term Task Roadmap
1. **Stabilise the Direct3D11 offscreen renderer**  
   * Finalise device/context creation, BGRA render target management, and CPU readback in `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.cpp`.  
   * Keep lifecycle deterministic and feed errors through the existing `getLastError()` surface.  
   * Extend/adjust `tests/yup_gui/yup_RiveOffscreenRenderer.cpp` as needed.

2. **Wire animation and state-machine control**  
   * Ensure `.riv` loading hooks populate animation/state-machine lists and drive advancement via `advance()` using existing YUP timing utilities (`modules/yup_core/`, `modules/yup_events/`).

3. **Build the pybind11 extension**  
   * Author bindings in `python/src/` (e.g., `python/src/yup_rive_renderer.cpp`), update `python/CMakeLists.txt`, and align packaging metadata in `python/pyproject.toml`.

4. **Create the Python NDI orchestration layer**  
   * Implement `python/yup_ndi/` modules that wrap the binding, integrate `cyndilib.Sender`, and provide configuration hooks.  
   * Add mocks/tests in `python/tests/` to exercise the flow without requiring an actual NDI runtime.

5. **Integrate tooling and documentation**  
   * Update `justfile`, `tools/` scripts, and `docs/` so Windows contributors can build the renderer, generate wheels, and run smoke tests end to end.

## Repository Map (Key Paths)
| Path | Purpose |
| --- | --- |
| `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.h/.cpp` | Core renderer implementation (current focus). |
| `modules/yup_gui/component/` | Higher-level GUI helpers—mine for reuse before adding new infrastructure. |
| `modules/yup_graphics/` | GPU helpers/textures; reuse for D3D11 plumbing. |
| `modules/yup_core/`, `modules/yup_events/` | Logging, timing, threading utilities useful for renderer orchestration. |
| `tests/yup_gui/yup_RiveOffscreenRenderer.cpp` | Current GoogleTest coverage; extend alongside renderer changes. |
| `python/CMakeLists.txt`, `python/pyproject.toml` | Build configuration for upcoming pybind11 module. |
| `python/src/` | Future home for binding sources. |
| `python/yup_ndi/` | Planned Python package for NDI orchestration. |
| `python/tests/` | Place Python unit/integration tests for bindings and NDI layer. |
| `docs/` | Update with Windows pipeline instructions once features land. |
| `tools/` & `justfile` | Developer tooling entry points; extend only when needed for renderer/binding/NDI workflows. |

### Out-of-Scope / Avoid Touching Unless Necessary
- `modules/yup_audio/*`, `modules/yup_plugin/*`, `examples/audio/*`, `examples/plugins/*`: Audio and plugin stacks stay untouched.
- `thirdparty/`: Treat as vendor code—no edits.
- Non-Windows build systems: keep existing support but do not broaden scope.

## Workflow Expectations
- Start each feature by surveying existing helpers to avoid duplication (e.g., reuse resource-management utilities under `modules/yup_graphics/`). Document discoveries in commit messages or comments.
- Maintain modular boundaries: renderer core in C++, bindings in `python/src/`, orchestration in `python/yup_ndi/`.
- Provide meaningful Doxygen for new public APIs (C++ & Python). Keep Allman brace style and follow include-order guidance from `CLAUDE.md`.
- Use RAII and existing `Result`/`ResultValue` types for error handling. Expose rich errors through `getLastError()` and propagate to Python bindings.
- Add deterministic tests for every new surface (C++ GoogleTests or Python `pytest`). When NDI can’t be exercised automatically, supply mocks and document manual steps.
- Keep changes focused; avoid formatting churn or incidental refactors outside scoped directories.

## Communication Notes
- In PR descriptions and status updates, tie progress to the roadmap checkpoints above. Call out any Visual Studio or packaging prerequisites you modify.
- Windows 11/MSVC 2022 remains the primary target—ensure instructions, scripts, and defaults respect that constraint.

Stay focused on delivering the Windows-ready Rive renderer pipeline while keeping the repository tidy and well documented.
