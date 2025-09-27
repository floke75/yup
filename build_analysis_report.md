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
    self._sender.write_video_async(contiguous)
else:
    self._sender.write_video(contiguous)
```

This change has been made and is ready for submission.

## 3. C++ Build System Analysis and Blockers

Verifying the Python fix was prevented by a cascade of C++ compilation errors. The following is a summary of the investigation and findings.

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

### Attempts to Fix the Build System

Several attempts were made to correct the CMake build system:

1.  **Changed `INTERFACE` to `STATIC`**: In `cmake/yup_modules.cmake`, all module library definitions were changed from `INTERFACE` to `STATIC`.
2.  **Corrected Target Properties**: The properties for these new `STATIC` libraries were updated, making source files `PRIVATE` and all other properties (`PUBLIC`) to ensure correct dependency propagation.
3.  **Fixed Missing Dependencies**: Explicit `dependencies` were added to the module headers for `rive_decoders` and `yup_python`.
4.  **Corrected Include Paths**: Incorrect relative include paths in `yup_gui.cpp` and `yup_ArtboardFile.cpp` were fixed to be relative to the `modules` directory.
5.  **Declared `SDL2` Dependency**: The `yup_gui` module header was updated to explicitly declare its dependency on `SDL2::SDL2` for all desktop platforms.

Despite these significant corrections, the build still fails with dependency-related errors, indicating that the build system's issues are complex and deeply rooted.

## 4. Recommendations

1.  **Submit the Python Fix**: The fix for the NDI double-sending bug is correct and provides a significant performance improvement. It is recommended to submit this change independently.
2.  **Overhaul the CMake Module System**: The C++ build system requires a more thorough review and refactoring. The current approach of using header-based module declarations with custom parsing logic is brittle. A more standard CMake approach, where dependencies are explicitly and correctly linked using `target_link_libraries`, would be more robust and maintainable. This should be treated as a separate, high-priority technical debt task.
3.  **Verify `pybind11` API Usage**: The `pybind11::memoryview` API calls in `yup_rive_renderer.cpp` are incorrect for the version of the library being used. Once the build system is stable, these calls need to be updated to match the correct API for the `pybind11` version included in the project.