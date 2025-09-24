# Coding Agent Guide: GPU-Accelerated Rive Renderer with NDI Output

## Mission Overview
You are extending YUP to deliver a Windows-focused pipeline that renders Rive (`.riv`) animations offscreen via Direct3D 11 and exposes frames (with alpha) to Python for NDI transmission. The end result is a reusable C++ core (built with CMake/Visual Studio), a pybind11-powered Python module, and a lightweight Python control layer that can drive NDI streams and optional REST/OSC endpoints. Audio/plugin subsystems in YUP remain untouched and disabled at runtime.

**Supported platform:** Windows 11 with MSVC 2022 and the Windows 10/11 SDK. Other platforms may compile stub implementations, but only Windows requires full functionality.

## Implementation Snapshot (April 2025)
| Area | Status | Key Files |
| --- | --- | --- |
| Direct3D 11 offscreen renderer | ✅ `yup::RiveOffscreenRenderer` initialises the D3D11 device/context, renders into a BGRA texture, and performs CPU readback via a configurable staging-buffer ring. | `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.h/.cpp` |
| Scene & animation control | ✅ Artboard enumeration, animation/state-machine playback, pause toggling, and state-machine input helpers are exposed through the renderer API. | `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.cpp` |
| Renderer tests | ✅ GoogleTest coverage validates frame stride/dimensions, pause semantics, shared-buffer behaviour, and artboard switching. | `tests/yup_gui/yup_RiveOffscreenRenderer.cpp` |
| Python renderer binding | ✅ `yup_rive_renderer` pybind11 module wraps renderer construction (including staging-buffer depth), artboard/animation APIs, and exposes zero-copy frame views. | `python/src/yup_rive_renderer.cpp`, build glue in `python/CMakeLists.txt`, packaging metadata in `python/pyproject.toml` |
| Python NDI orchestration | ✅ `yup_ndi` package manages multi-stream orchestration, timestamp mapping, metadata dispatch, and optional control hooks using mocked `cyndilib` senders for tests while forwarding renderer options such as staging-buffer depth. | `python/yup_ndi/__init__.py`, `python/yup_ndi/orchestrator.py`, tests under `python/tests/test_yup_ndi/` |
| Python test harness | ✅ `pytest` suites cover binding behaviour, orchestrator frame flow, and provide fake renderer/sender utilities that avoid native DLL requirements. | `python/tests/` |
| Documentation & developer workflow | ⚠️ Needs expansion in `docs/` and `tools/` to describe Windows build steps, wheel packaging, and orchestration usage. |

## Documentation Cross-References
- **Quick orientation:** Start with the redundancy-pruning checklist in `README.md` to understand the trim plan for legacy modules and the guardrails that must remain while focusing on the Rive → NDI path.
- **Rive → NDI flow:** The canonical walkthrough lives in `docs/Rive to NDI Guide.md`. It maps renderer classes to Python bindings, calls out the minimal module surface needed for transmission, and links TODOs for each subsystem.
- **Legacy dependency audit:** Inline breadcrumbs inside `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.*` and `python/yup_ndi/orchestrator.py` enumerate bindings/tests that must stay aligned if refactors delete surrounding helpers.
- **Testing expectations:** Use the guidance embedded in `python/tests/conftest.py` and `python/tests/common.py` to respect the mock strategy for environments without the native extension. Honour the skip markers before trimming any "legacy" fixtures that keep pytest green.
- **Build/packaging recipes:** Use the pointers in `tools/` and the notes in `python/pyproject.toml` to keep MSVC/Ninja build flags, version pinning, and wheel metadata consistent with the Windows toolchain assumptions documented above.

## Codebase Map
Use this quick-reference map to locate the canonical implementations before introducing new functionality. Reviewing these areas first helps avoid duplicating existing work.

### Core C++ Renderer & Engine
- `modules/yup_gui/artboard/`: Rive offscreen renderer implementation (`yup_RiveOffscreenRenderer.*`), scene management helpers, texture readback, and animation control utilities.
- `modules/yup_gui/native/`: Platform-specific glue for Windows. Non-Windows builds contain stub placeholders—extend only when broadening platform support.
- `modules/yup_core/` and `modules/yup_graphics/`: Shared engine primitives (logging, result types, graphics context helpers) that the renderer depends on. Prefer extending these modules over creating parallel abstractions.

### Python Bindings & Orchestration
- `python/src/`: pybind11 bindings that expose renderer capabilities to Python. `yup_rive_renderer.cpp` is the authoritative binding surface.
- `python/yup_ndi/`: High-level orchestration for NDI output, metadata handling, and stream control logic.
- `python/tests/`: `pytest` suites covering bindings, orchestration, and mock senders. Update or extend tests alongside feature changes.

### Tooling, Examples, and Docs
- `tools/`: Build, packaging, and CI helpers (`package_wheel.py`, environment setup scripts).
- `docs/`: Conceptual guides, including the Rive → NDI walkthrough. Add new workflow documentation here.
- `examples/`: Minimal end-to-end usage samples. Use these as starting points when demonstrating new capabilities.

### Ancillary Assets
- `standalone/`: Sandbox applications and experiments. Reuse renderer/pipeline components instead of rebuilding them here.
- `thirdparty/`: External dependencies (e.g., Rive SDK, pybind11). Confirm licence compatibility before introducing new vendored code.
- `cmake/` and `CMakeLists.txt`: Build system entry points. Integrate new modules by extending the existing CMake structure rather than creating ad-hoc scripts.

## Coding Standards & Structure
The following conventions apply to all new or modified source files within this repository:

### Required File Header
Start every new source file with the exact ISC licence banner below. Update legacy files if they are missing it when you touch them.

```cpp
/*
  ==============================================================================

   This file is part of the YUP library.
   Copyright (c) 2025 - kunitoki@gmail.com

   YUP is an open source library subject to open-source licensing.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   to use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   YUP IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/
```

### Module Header Block
Primary module headers (e.g., `yup_graphics.h`) must also include the declaration block below immediately after the licence banner. Update `module_name` and other fields appropriately.

```cpp
/*
  ==============================================================================

  BEGIN_YUP_MODULE_DECLARATION

    ID:                 module_name
    vendor:             yup
    version:            1.0.0
    name:               Module Display Name
    description:        Brief module description
    website:            https://github.com/kunitoki/yup
    license:            ISC
    minimumCppStandard: 17

    dependencies:       yup_graphics [other_dependencies]
    searchpaths:        native
    enableARC:          1

  END_YUP_MODULE_DECLARATION

  ==============================================================================
*/
```

### Formatting, Naming, and Includes
- Follow Allman-style braces for all scopes.
- Use `PascalCase` for classes, `camelCase` for functions, variables, constants, and member fields.
- Match file names to the primary class (e.g., `yup_ClassName.h/.cpp`).
- Keep include order as follows: own header (if in a `.cpp`), C++ standard library, external dependencies (Rive, etc.), other YUP modules, and finally headers from the same module.
- Avoid `using namespace` in headers; limit its use to the narrowest possible scope within implementation or test files.

### File & Test Organisation
- Modules should remain shallow: a top-level header/implementation pair with a single nested directory for related classes and a `native/` folder for per-platform sources.
- Unit tests live under `tests/<module_name>/` and should be grouped by class or integration scenario.

## Design & Implementation Principles
- Prefer RAII and smart pointers for resource management.
- Use `std::optional` for optional values and `yup::String` for most string operations (use `std::string` only when bridging external APIs).
- Keep classes narrowly focused, apply const-correctness, and favour composition over inheritance.
- Guard platform-specific code with the appropriate `#if YUP_<PLATFORM>` macros and provide functional Windows implementations plus stubs elsewhere.
- Surface errors through `yup::Result`/`yup::ResultValue<T>` patterns and mirror renderer errors via `RiveOffscreenRenderer::getLastError()` in Python exceptions.
- Document all public APIs with Doxygen comments and maintain American English spelling (`color`, `center`, etc.).

## Testing Expectations
- Extend GoogleTest coverage for C++ changes and `pytest` suites for Python updates. Provide mocks when native dependencies (NDI, GPU) are unavailable in CI.
- Coordinate integration testing across the renderer and Python orchestration layers; smoke-test the pybind11 bindings feeding the orchestrator when possible.
- When touching test infrastructure, respect the skip markers and helper utilities in `python/tests/` to keep environments without the native extension passing.

## Current Priorities
1. **Documentation & tooling:** Expand Windows-specific build instructions in `docs/` and automate wheel packaging and orchestration workflows via `tools/` or updated `just` recipes.
2. **Integration testing:** Add or extend combined tests that exercise the renderer bindings feeding the NDI orchestrator (mocked senders acceptable in CI).
3. **Performance validation:** Profile renderer → Python throughput to ensure zero-copy semantics on Windows hardware.

# Packaging
- Run `python tools/package_wheel.py` to produce a release-mode build and wheel.

## Workflow Expectations
- Analyse existing helpers (e.g., `modules/yup_graphics/`, `modules/yup_core/`) before introducing new abstractions; reuse utilities wherever feasible.
- Keep build flags, documentation, and scripts aligned with Windows 11 + MSVC 2022 assumptions. Non-Windows builds may remain stubbed but must compile.
- Maintain focused change sets—avoid formatting-only commits and unrelated subsystem modifications.

Stay focused on delivering a polished Windows pipeline from Rive rendering through Python-based NDI streaming while maintaining clear documentation for future contributors.
