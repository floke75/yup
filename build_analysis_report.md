# Rive-to-NDI Pipeline and Build System Analysis Report

## 1. Executive Summary

The initial task was to analyze the alignment and implementation state of the Rive-to-NDI pipeline. This investigation uncovered a critical, performance-impacting bug in the Python orchestration layer. While fixing this bug, a series of deeper issues within the C++ native module's build system were discovered, which currently prevent the project from being built and tested.

This report details both the Python-level fix and the extensive findings related to the C++ build system blockers.

## 2. Python NDI Orchestrator Fix

A significant "alignment" issue was identified in `python/yup_ndi/orchestrator.py` within the `_CyndiLibSenderHandle.send` method.

### The Bug: Double Frame Sending

The original implementation was sending every video frame to NDI twice:

```python
# Original incorrect code
contiguous = buffer.cast("B")
self._sender.write_video(contiguous) # First send (synchronous)
if self._use_async:
    self._sender.send_video_async() # Second send (asynchronous)
else:
    self._sender.send_video()
```

According to the `cyndilib` documentation, `write_video()` and `send_video_async()` are mutually exclusive operations for sending a frame. `write_video()` writes the data *and* sends it synchronously. `send_video_async()` is intended to be used after writing data to the frame buffer via a different mechanism. The most efficient method for asynchronous sending is a single call to `write_video_async(data)`.

This bug caused unnecessary network traffic and CPU/GPU overhead.

### The Fix

The code was corrected to use the appropriate `cyndilib` function based on the `_use_async` flag, eliminating the redundant send:

```python
# Corrected code
contiguous = buffer.cast("B")
if self._use_async:
    write_video_async = getattr(self._sender, "write_video_async", None)
    if callable(write_video_async):
        write_video_async(contiguous)
    else:
        _logger.debug(
            "cyndilib sender does not expose write_video_async; falling back to synchronous send"
        )
        self._sender.write_video(contiguous)
else:
    self._sender.write_video(contiguous)
```

This change has been made and now gracefully falls back to the synchronous
`write_video()` path when older `cyndilib` builds do not expose
`write_video_async`, ensuring the frame is still transmitted without the
duplicate send behaviour that triggered this investigation.

## 3. C++ Build System Analysis and Blockers

Verifying the Python fix initially surfaced a cascade of C++ compilation errors. The investigation below documents those
failures and the remediation now in place.

### Initial Build Failures & Fixes

1.  **Missing `alsa` dependency**: The initial build failed because the ALSA development libraries were not found. This was bypassed by setting the `YUP_ENABLE_AUDIO_MODULES=0` environment variable, as documented in `docs/Rive to NDI Guide.md`.
2.  **Missing `curl` dependency**: The build then failed with `curl/curl.h: No such file or directory`. This was resolved by installing the `libcurl4-openssl-dev` system package.
3.  **Redundant C++ Includes**: The build then failed with multiple redefinition errors. This was traced to `python/src/yup_rive_renderer.cpp` including core headers that were already part of a central include (`yup_PyBind11Includes.h`). Removing the redundant includes fixed this specific issue.

### Root Cause: Flawed CMake Module System

After fixing the initial issues, a deeper, more fundamental problem was uncovered in the project's CMake configuration (`cmake/yup_modules.cmake`).

The core issue is that modules were defined as `INTERFACE` libraries, with their source files also declared as `INTERFACE`. This is an incorrect use of CMake's `INTERFACE` library feature. It caused any target linking a module to recompile all of that module's source files directly, but crucially, **without** inheriting the module's own dependencies.

This led to a cascade of "header not found" errors, such as:
*   `rive/rive_types.hpp: No such file or directory` in `rive_decoders`.
*   `SDL2/SDL.h: No such file or directory` in `yup_gui`.
*   `yup_core/containers/yup_MemoryBlock.h: No such file or directory` in `yup_gui`.
*   `#error This binding file requires adding the yup_events module in the project` in `yup_python`.

### CMake Module System Remediation

The build now succeeds in configuring the module graph after refactoring the bespoke module helper to emit normal static
libraries:

1.  **Converted Module Targets to `STATIC`**: Every module defined via `_yup_module_setup_target` is materialised as a static
    library, ensuring that each translation unit is compiled exactly once and eliminating multiple-definition failures.
2.  **Propagated Usage Requirements**: Compile features, options, definitions, include paths, link directories, link options and
    dependent libraries are now exposed with `PUBLIC` scope so consumers inherit the correct build settings automatically.
3.  **Enabled Position Independent Code**: Modules are compiled with `POSITION_INDEPENDENT_CODE` to keep the resulting archives
    linkable into the project’s shared libraries (such as the pybind11 extension) on POSIX platforms.
4.  **Retained Module Metadata**: The existing declaration parsing and dependency wiring remains intact, so higher-level CMake
    code does not need to change its module definitions.

With these adjustments, the configuration phase now correctly resolves module dependencies without duplicating compilation. On
Linux environments without the SDL2 development package installed, the build now progresses until the `yup_gui` target attempts
to include `SDL2/SDL.h`; installing the dependency (or building on Windows, where the Direct3D implementation is primary) is
still required to complete the native build.

## 4. Recommendations

1.  **Submit the Python Fix**: The fix for the NDI double-sending bug is correct and provides a significant performance improvement. It is recommended to submit this change independently.
2.  **Overhaul the CMake Module System**: The C++ build system requires a more thorough review and refactoring. The current approach of using header-based module declarations with custom parsing logic is brittle. A more standard CMake approach, where dependencies are explicitly and correctly linked using `target_link_libraries`, would be more robust and maintainable. This should be treated as a separate, high-priority technical debt task.
3.  **Verify `pybind11` API Usage**: The `pybind11::memoryview` API calls in `yup_rive_renderer.cpp` are incorrect for the version of the library being used. Once the build system is stable, these calls need to be updated to match the correct API for the `pybind11` version included in the project.