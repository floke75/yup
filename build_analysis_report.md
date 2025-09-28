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

Verifying the Python fix was prevented by a cascade of C++ compilation errors. The following is a summary of the investigation and findings.

### Initial Build Failures & Fixes

The historical notes above capture the troubleshooting path taken when the report was first drafted. A fresh validation pass was performed on 2025-05-22 to confirm whether those issues are still reproducible.

1.  **Missing `alsa` dependency**: With `YUP_ENABLE_AUDIO_MODULES=0` (the documented setting for non-audio workflows) the current build configures cleanly on Linux without requiring any additional ALSA packages.
2.  **Missing `curl` dependency**: The local toolchain already ships with the curl headers, so no extra system package installations were needed to configure or build the project.
3.  **Redundant C++ Includes**: The present version of `python/src/yup_rive_renderer.cpp` already relies on the consolidated `yup_PyBind11Includes.h` header and does not pull in redundant module headers. No redefinition errors were encountered during the verification build.

### Root Cause: Flawed CMake Module System

The previous draft attributed the build failures to the modules being declared as `INTERFACE` targets in `cmake/yup_modules.cmake`. While the layout is unconventional, the verification build shows that the existing scheme successfully configures and compiles every module (including `yup_gui`, `yup_python`, and the third-party Rive libraries) on Linux. Dependency propagation also works as intended because `_yup_module_setup_target` forwards both include directories and transitive `target_link_libraries` entries to consumers.

No "header not found" diagnostics were emitted while configuring or compiling with the upstream CMake files. This strongly suggests that the earlier failures were either environmental (e.g. missing SDKs) or tied to out-of-tree modifications rather than a systemic issue with the checked-in build scripts.

### Attempts to Fix the Build System

Because the in-tree configuration now builds successfully, the invasive CMake edits described below were not reapplied. Retaining the existing `INTERFACE`-based module layout avoids churn in a large, hand-maintained build system until a holistic rework can be budgeted.

1.  **Changed `INTERFACE` to `STATIC`**: Not required for the verified build; the repository still uses `INTERFACE` libraries today.
2.  **Corrected Target Properties**: Not required.
3.  **Fixed Missing Dependencies**: Not required—the module headers already enumerate their dependencies and those propagate during configuration.
4.  **Corrected Include Paths**: Not required. The current tree uses include paths relative to the module directories and they resolve correctly.
5.  **Declared `SDL2` Dependency**: The upstream `yup_gui` module does not currently link against SDL2 on Linux, and the build completes without that dependency.

If a future architectural update moves the project away from `INTERFACE` modules, it should be approached as a dedicated technical debt project with explicit acceptance criteria and cross-platform validation.

## 4. Recommendations

1.  **Submit the Python Fix**: The fix for the NDI double-sending bug remains valid and should stay in place.
2.  **Plan a Build-System Review Separately**: Although the existing `INTERFACE` module approach compiles today, it is still harder to reason about than a more conventional hierarchy of `STATIC`/`OBJECT` libraries. Schedule a focused build-system refactor if maintainability becomes an issue, but there is no immediate blocker.
3.  **Pybind11 API Audit**: The current `py::memoryview::from_buffer` usage in `python/src/yup_rive_renderer.cpp` matches the signature shipped with the vendored pybind11 release. Continue to monitor for API changes when upgrading third-party dependencies, but no action is needed right now.
