# Rive-to-NDI Pipeline and Build System Status Report

## 1. Current Status Overview
- The Python orchestration layer now performs a single write per frame and gracefully falls back when asynchronous sending is unavail
  able, eliminating the duplicate transmit bug that originally triggered this investigation.【F:python/yup_ndi/orchestrator.py†L522-
  L567】
- The `yup_rive_renderer` binding exposes frame buffers through `py::memoryview::from_buffer`, pairing explicit shape/stride data w
  ith a lifetime-managed capsule so that Python receives a standards-compliant view of the renderer output.【F:python/src/yup_rive_r
  enderer.cpp†L69-L131】
- CMake now materialises modules with real sources as static libraries, so Windows builds compile each module once and reuse the resulting archives. Dependency propagation still relies on the metadata baked into each module header, so coverage for missing `find_package` stanzas remains an outstanding item.【F:cmake/yup_modules.cmake†L196-L331】

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

### Module Compilation Hardening
- `yup_add_module` now creates static libraries whenever a module contributes compilable sources and pushes its include paths,
  compile options, and dependent libraries through a `PUBLIC` usage scope. Downstream targets link a single archive instead of
  rebuilding interface sources on every consume call, eliminating the link-time churn previously observed on Windows.【F:cmake/
  yup_modules.cmake†L196-L331】

## 3. Outstanding Build-System Risks
1.  **Manual dependency wiring:** Because dependencies are passed as raw generator expressions, any mismatch between module declarations and `_yup_module_setup_target` arguments results in missing `target_link_libraries` entries. This design still requires auditing each module header to confirm its `dependencies` stanza matches the actual code usage.【F:cmake/yup_modules.cmake†L196-L331】【F:cmake/yup_modules.cmake†L360-L416】
2.  **Third-party discovery friction:** External libraries (e.g., SDL2, Rive SDK) rely on consumers to supply the correct include roots via `target_include_directories(... INTERFACE ...)`, so the build continues to fail on clean machines until the module metadata is normalized or the CMake targets grow explicit `find_package` helpers for each third-party dependency.【F:cmake/yup_modules.cmake†L172-L231】【F:cmake/yup_modules.cmake†L360-L416】

## 4. Recommended Next Steps
1.  Introduce regression tests (CI or `just` recipes) that configure and build the minimal Rive-to-NDI pipeline on a clean environment, catching missing package declarations as soon as a module header changes.
2.  Extend the documentation in `docs/Rive to NDI Guide.md` with a troubleshooting appendix that maps common build errors back to the module whose metadata triggered them once the module graph has been stabilised.

## 5. Tracking Notes
- ALSA remains optional; keep `YUP_ENABLE_AUDIO_MODULES=0` in Linux CI until audio-side dependencies are revisited.
- Continue mirroring updates from `_yup_module_setup_target` into `_yup_module_setup_plugin_client` whenever new arguments are add
  ed so plugin builds stay aligned.
