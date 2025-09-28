# Rive-to-NDI Pipeline and Build System Status Report

## 1. Current Status Overview
- The Python orchestration layer now performs a single write per frame and gracefully falls back when asynchronous sending is unavail
  able, eliminating the duplicate transmit bug that originally triggered this investigation.【F:python/yup_ndi/orchestrator.py†L522-
  L567】
- The `yup_rive_renderer` binding exposes frame buffers through `py::memoryview::from_buffer`, pairing explicit shape/stride data w
  ith a lifetime-managed capsule so that Python receives a standards-compliant view of the renderer output.【F:python/src/yup_rive_r
  enderer.cpp†L69-L131】
- CMake still models every module as an `INTERFACE` library and injects sources, options, and dependencies through `_yup_module_se
  tup_target`, so the original transitive-linking issues remain unresolved and are the primary blocker to reliable native builds.
  【F:cmake/yup_modules.cmake†L172-L244】【F:cmake/yup_modules.cmake†L300-L360】

## 2. Completed Items
### Python NDI Orchestrator
- `_CyndiLibSenderHandle.send` caches `write_video_async`, uses it when present, and logs the synchronous fallback only once, removing
  the redundant send path and reducing per-frame overhead.【F:python/yup_ndi/orchestrator.py†L522-L567】

### Pybind11 Binding Hygiene
- `makeFrameMemoryView` now returns either an empty sentinel view or a multi-dimensional BGRA view backed by a `std::shared_ptr` cap
  sule, matching current `pybind11` expectations and avoiding the API misuse called out in the earlier audit.【F:python/src/yup_rive
  _renderer.cpp†L69-L131】

### CMake Helper Consistency
- `_yup_module_setup_plugin_client` forwards the availability flag to `_yup_module_setup_target`, ensuring plugin client variants
  inherit the same compile-time guards as their parent module—a regression that has already been corrected upstream.【F:cmake/yup_
  modules.cmake†L248-L336】

## 3. Outstanding Build-System Risks
1.  **Interface-only modules:** Every module is still declared as an `INTERFACE` target with its sources injected via `target_sour
    ces(... INTERFACE ...)`. CMake treats these files as headers, so each consumer recompiles them and must manually propagate the
    same dependency graph, recreating the include/link churn that blocked end-to-end builds during the original analysis.【F:cmake/
    yup_modules.cmake†L172-L244】
2.  **Manual dependency wiring:** Because dependencies are passed as raw generator expressions, any mismatch between module decla
    rations and `_yup_module_setup_target` arguments results in missing `target_link_libraries` entries. This design still require
    s auditing each module header to confirm its `dependencies` stanza matches the actual code usage.【F:cmake/yup_modules.cmake†L
    172-L244】【F:cmake/yup_modules.cmake†L360-L416】
3.  **Third-party discovery friction:** External libraries (e.g., SDL2, Rive SDK) rely on consumers to supply the correct include
    roots via `target_include_directories(... INTERFACE ...)`, so the build continues to fail on clean machines until the module me
    tadata is normalized or the CMake targets are converted to `STATIC`/`OBJECT` libraries with explicit linkage.【F:cmake/yup_modul
    es.cmake†L172-L231】【F:cmake/yup_modules.cmake†L360-L416】

## 4. Recommended Next Steps
1.  Rework module targets as `OBJECT` or `STATIC` libraries so that compilation happens once per module and dependency linkage be
    comes declarative. Prioritize `yup_gui`, `yup_python`, and `rive_decoders`, as they currently surface the majority of missing-
    header failures when configuring sample builds.
2.  Introduce regression tests (CI or `just` recipes) that configure and build the minimal Rive-to-NDI pipeline on a clean enviro
    nment, catching missing package declarations as soon as a module header changes.
3.  Extend the documentation in `docs/Rive to NDI Guide.md` with a troubleshooting appendix that maps common build errors back to
    the module whose metadata triggered them once the module graph has been stabilised.

## 5. Tracking Notes
- ALSA remains optional; keep `YUP_ENABLE_AUDIO_MODULES=0` in Linux CI until audio-side dependencies are revisited.
- Continue mirroring updates from `_yup_module_setup_target` into `_yup_module_setup_plugin_client` whenever new arguments are add
  ed so plugin builds stay aligned.
