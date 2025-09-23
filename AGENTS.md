# Coding Agent Guide: GPU-Accelerated Rive Renderer with NDI Output

## Mission Overview
You are extending YUP to deliver a Windows-focused pipeline that renders Rive (.riv) animations offscreen via Direct3D 11 and exposes frames (with alpha) to Python for NDI transmission. The end result is a reusable C++ core (built with CMake/Visual Studio), a pybind11-powered Python module, and a lightweight Python control layer that can drive NDI streams and optional REST/OSC endpoints. Audio/plugin subsystems in YUP remain untouched and disabled at runtime.

### Key Objectives
1. **C++ Offscreen Renderer**: Maintain or extend `yup::RiveOffscreenRenderer` (or a sibling module) to initialise a D3D11 device without a swap chain, render artboards into a BGRA texture, and provide fast CPU readback.
2. **Rive Animation Control**: Build a wrapper that loads .riv files, manages artboards/animations/state machines, and coordinates frame advancement with rendering.
3. **Python Bindings**: Expose the renderer/animation engine through pybind11 (module name TBD, e.g. `yup_rive_renderer`) with ergonomic methods for animation control and frame retrieval.
4. **NDI Streaming Layer**: Implement Python orchestration that consumes BGRA frames and publishes them via cyndilib’s `Sender`, supporting multi-instance operation and optional REST/OSC control.
5. **Windows Tooling**: Ensure all CMake targets and build scripts work with MSVC 2022, C++17, and the Windows 10/11 SDK. Other platforms are secondary, and shipping/support expectations are limited to Windows 11.

Keep these objectives in mind while navigating the codebase; most of YUP is unrelated to this pipeline.

## Repository Map (Relevant Areas)
| Path | Why it matters |
| --- | --- |
| `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.h/.cpp` | Existing offscreen renderer skeleton. Extend/refactor here to fulfil the D3D11 BGRA rendering and readback requirements. |
| `modules/yup_gui/component/` | Hosts higher-level GUI abstractions; only touch if you need factory hooks or resource loading helpers for Rive. |
| `modules/yup_graphics/` | Low-level graphics helpers (textures, render contexts). Useful when wiring Direct3D resources or reusing YUP utilities. |
| `modules/yup_core/` & `modules/yup_events/` | General utilities (timers, logging, threading). Reuse when building engine support code. |
| `python/CMakeLists.txt` & `python/pyproject.toml` | Starting points for configuring the pybind11 extension and packaging the Python module. |
| `python/tests/` | Place or adapt tests that validate the Python binding and NDI integration stubs. |
| `tools/` | Contains build scripts and helper utilities; add new tooling (e.g., packaging commands) if necessary. |
| `examples/render/` | Reference for how YUP currently integrates Rive; use for implementation hints but do not modify unless creating dedicated demos. |

### Out-of-Scope / Mostly Redundant for This Project
- `modules/yup_audio/*`, `modules/yup_plugin/*`: Audio/plugin layers should remain untouched.
- `examples/audio/*`, `examples/plugins/*`: Skip unless documenting non-changes.
- `thirdparty/` contents: Treat as vendor code—do not edit.
- Platform builds other than Windows: keep existing settings but avoid platform-specific churn.

## Workflow Expectations
- Maintain modular design: isolate the D3D11 renderer, animation logic, Python binding, and NDI orchestrator into clear components with minimal coupling.
- Prefer extending existing facilities over reinventing them (e.g., reuse `RiveOffscreenRenderer` scaffolding, CMake helper functions, and logging macros).
- Document behaviour and assumptions inline (especially around GPU resource lifetimes and frame timing).
- Add targeted tests (C++ or Python) to cover frame generation, animation advancement, and binding correctness. If NDI cannot be exercised in CI, stub or mock responsibly and note manual validation steps.
- Leave unrelated subsystems exactly as they are; avoid incidental formatting or drive-by refactors outside the scope above.


## Coding Standards (See also `CLAUDE.md`)
- **File headers**: Every new C++ source or header file must begin with the canonical YUP comment block defined in `CLAUDE.md`. Do not omit or alter the wording.
- **Formatting**: Follow Allman brace style for classes, functions, and control structures. Keep indentation consistent with existing code.
- **Naming**: Use PascalCase for classes, camelCase for functions, variables, and constants. Keep one primary class per file (`yup_ClassName.h/.cpp`).
- **Includes & guards**: Honour the include order guidance in `CLAUDE.md`, and prefer `#pragma once` in headers.
- **Const-correctness & RAII**: Prefer immutable interfaces where possible, wrap resources in RAII helpers, and use YUP `Result`/`ResultValue` patterns for fallible operations.
- **Documentation**: Provide meaningful Doxygen comments for any new public API you surface in C++ or Python bindings.
- **Testing**: When adding tests, focus on public interfaces, keep them deterministic, and use `just test` (or the project’s documented equivalent) to execute them when requested.
- **Platform guards**: Wrap platform-specific code with the `YUP_*` macros outlined in `CLAUDE.md`.
- **String & optional utilities**: Prefer `yup::String`, `std::optional`, and other YUP-standard abstractions when handling dynamic values.

## Communication Notes
- When creating pull requests or status updates, always tie progress back to the objectives listed here (renderer, animation engine, Python binding, NDI layer, Windows build support).
- Call out dependencies or configuration changes that impact Visual Studio builds or Python packaging so downstream consumers can adjust quickly.

Stay focused on the GPU-accelerated Rive rendering pipeline and keep the footprint of changes tight within the mapped areas.

