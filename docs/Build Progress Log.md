# Build Progress & Troubleshooting Log

## Status Overview (2025-09-29)
- Native Windows build (cmake --build build --config Release --target ALL_BUILD) now succeeds after restoring SDL2 linkage with automatic fetching and module metadata updates.
- Python tests succeed against the source tree (pytest python/tests → 32 passed, 62 skipped) using the mocked extension.
- Editable install now produces yup.cp311-win_amd64.pyd via the shared output-directory helper; packaging runs still require python -m build to be available in the active interpreter.

## Key Fixes Implemented
- **SDL2 integration**: CMake configuration fetches SDL2 on Windows, and yup_gui declares the sdl2::sdl2 dependency while all includes were normalized to <SDL/...>.
- **Rive renderer parity**: RiveOffscreenRenderer now aligns with the vendored SDK (no 
actory() accessor, default Mat2D, mutable frame queues, out-of-class describeMapFailure). A std::vector<uint8> overload supports zero-copy handoff to Python.
- **Binding cleanup**: Python bindings wrap String results in std::string, reuse the new overload for byte loads, and construct memory-views with the modern pybind11 API. Includes collapse to yup_core/yup_core.h.
- **Setup adjustments**: Editable builds default to audio-free, fetch SDL2 automatically, and pass CMAKE_RUNTIME_OUTPUT_DIRECTORY so binaries share an output root. PYI generation now runs by default; set `YUP_GENERATE_PYI=0` to skip stub generation when tooling is unavailable.
- **Wheel staging**: python/CMakeLists now applies CMAKE_*_OUTPUT_DIRECTORY values to the yup and yup_rive_renderer targets, and tools/package_wheel.py enables YUP_BUILD_WHEEL with module exports disabled so the .pyd lands beside setup.py.
- **Documentation**: Windows build guide highlights automatic SDL2 fetching; this log tracks build context for future sessions.

## Current Blockers
1. Decide packaging surface: clarify whether the final wheel will expose the monolithic yup module, only yup_rive_renderer, or both.
2. Expand documentation into a full troubleshooting guide after the editable install path is stable.

## Session Notes
- 2025-10-04 11:45: Re-enabled stub generation during setup builds (`YUP_GENERATE_PYI=1` by default) and ensured the wheel carries `yup_rive_renderer` alongside the monolithic extension.
- 2025-10-04 11:10: Added yup_apply_output_directories helper so CMake respects CMAKE_RUNTIME/LIBRARY output hints; tools/package_wheel.py now opts into YUP_BUILD_WHEEL with exports disabled and copies the generated yup_rive_renderer.pyd after a successful Release build.
- 2025-09-29 16:45: Editable install still failing; setup.py now copies modules relative to CMAKE_RUNTIME_OUTPUT_DIRECTORY but no .pyd is emitted. Next session should investigate the CMake yup target outputs and adjust accordingly.
- 2025-09-29 22:28: Verified local toolchain before end-to-end streaming effort; .venv Python 3.11.9 present, cmake 4.1.0 available. Noted ninja missing from PATH (will rely on MSBuild or install later) and NDI runtime environment variables already pointing at 'C:\Program Files\NDI\NDI 6 Runtime\v6'.
- 2025-09-29 22:31: Configured Visual Studio 2022 build with wheel flags and built Release artifacts; yup_rive_renderer.pyd generated under build/python/Release. No CTest targets materialized (expected due to audio modules disabled).
- 2025-09-29 22:31: Attempted pytest binding smoke with PYTHONPATH/PATH pointing at build/python/Release; ImportError persists (dependent DLL resolution). Will revisit after wheel install.
- 2025-09-29 22:34: Installed cyndilib editable build into project venv and confirmed Sender open/close flow succeeds (without ndi_groups). Non-empty group parameter currently triggers process exit; will avoid until investigated.
- 2025-09-29 22:43: Ran tools/package_wheel.py with venv interpreter, installed wheel into .venv. Removed stray .venv\\yup_rive_renderer.pyd (pathfinder preferred it and caused ImportError); confirmed import now succeeds.
- 2025-09-29 23:42: Added D3D11 feature-level fallback and defensive error handling in yup_RiveOffscreenRenderer; reconfigured CMake to target Python 3.11 venv. Current imports raise `ValueError('bad allocation')` even with the local RTX 5090 available, indicating we need deeper instrumentation (adapter enumeration, D3D11CreateDevice HRESULT capture, debug-layer output) to pinpoint why allocation is failing despite the RTX 5090.
## Session Notes (2025-10-05)
- Added defensive D3D11 initialisation: fallback retries, detailed error propagation, and resource cleanup live in modules/yup_gui/artboard/yup_RiveOffscreenRenderer.cpp. Python bindings now translate constructor failures into ValueError so CLI/scripts see actionable messages.
- Confirmed cyndilib editable install works; synchronous sender flows succeed, but group configuration may still abort (investigation deferred until after renderer import is stable).
- Built Release wheel with python tools/package_wheel.py using the project .venv; removed stray .venv\yup_rive_renderer.pyd that previously confused PathFinder during imports.
- Added python/examples/run_rive_ndi.py and documented it across README + docs (docs/rive_ndi_overview.md, docs/Rive to NDI Guide.md). Script mirrors CLI flags and surfaces D3D failures immediately.
- Ran pytest python/tests/test_yup_rive_renderer/test_binding_interface.py -q (reports skips on unsupported GPU setups) and pytest python/tests/test_yup_ndi -q (32 passed). No end-to-end NDI validation yet because the current host lacks a working D3D11 device.
- Reconfigured CMake to honour -DPython_EXECUTABLE so native builds target the same interpreter as the Python tests/wheel, avoiding mismatched ABI issues (e.g., Python 3.13 vs 3.11).

### Outstanding Issues
- Current host (equipped with an NVIDIA GeForce RTX 5090) still raises `ValueError: Failed to initialise RiveOffscreenRenderer: bad allocation`. Both hardware and WARP paths refuse to create a D3D11 device, pointing to a deeper D3D11 offscreen initialisation problem that we must diagnose with adapter enumeration, HRESULT logging, and debug-layer output. Investigate device creation flags or adapter selection on the next session.
- Because renderer initialisation fails, no real NDI send/receive loop has been exercised. Once GPU access is available, rerun python/examples/run_rive_ndi.py with a known .riv and verify in NDI Studio Monitor. Capture HRESULTs if failures persist.

### Recommended Follow-up
- After the instrumentation lands, rerun the Release build (cmake --build build --config Release) on this RTX 5090 workstation, smoke the example script + CLI, and record the captured HRESULT/adapter logs for the troubleshooting docs.
- Instrument RiveOffscreenRenderer initialisation to log adapter names, feature levels, and raw D3D11CreateDevice HRESULTs (including debug-layer messages); surface these via getLastError() so Python callers and docs capture precise diagnostics.
- Once rendering succeeds, extend the pytest suite with an integration test that exercises NDIOrchestrator using the real renderer under a skipif guard keyed on D3D availability.

- Reminder: stick with Visual Studio 2022 / v143 toolset; earlier VS versions are not validated for this branch.
