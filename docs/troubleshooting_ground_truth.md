# Rive → NDI Troubleshooting Ground Truth

This log captures environment facts, diagnostic entry points, and references that
must remain accurate between sessions. Update it whenever new evidence arrives so
future agents do not re-hash disproven assumptions.

## Verified Environment Facts
- Windows 11 workstation with an NVIDIA GeForce RTX 5090 (desktop chassis, not a headless VM).
- Local display outputs are active; offscreen rendering failures are not caused by "headless" GPU policies.
- Visual Studio 2022 with the v143 toolset is installed and should be used for native builds.
- NDI SDK 6 is available under `C:\Program Files\NDI\NDI 6 SDK`, and NDI Tools live under `C:\Program Files\NDI\NDI 6 Tools`.

## Diagnostics Checklist
1. **Direct3D sanity check:** Run `tools\run_d3d11_diagnostics.cmd` (or `python tools/check_d3d11_device.py`) from a Windows Python shell to confirm `D3D11CreateDevice` succeeds. Use `--warp` to isolate WARP fallback behaviour when hardware initialisation fails. (Running the script inside WSL/Git-Bash fails because `ctypes.windll` is unavailable.)
2. **Renderer construction:** Instantiate `yup_rive_renderer.RiveOffscreenRenderer` with `enable_presentation=True` to surface swap-chain diagnostics and capture the new `get_diagnostics()` report if construction fails.
3. **Presentation mirror:** When the renderer initialises, toggle the preview window to ensure the staging buffers and swap-chain copy path are healthy before plumbing NDI sends.
4. **NDI orchestration:** Exercise `tools\run_rive_demo.cmd` (or `python/examples/run_rive_ndi.py --present-preview`) with a known-good `.riv` file once D3D initialisation succeeds so the orchestrator and sender plumbing are validated together.
5. **Log capture:** Persist the renderer diagnostics (`get_diagnostics()`), orchestrator error logs, and any HRESULTs observed in the Visual Studio Output window; attach them to this file or `docs/Build Progress Log.md` for future reference.

## Latest Diagnostics (2025-10-05)
- `py -3.11 tools\check_d3d11_device.py` fails immediately with `0x80070057 (The parameter is incorrect.)` for both hardware and WARP drivers. This confirms `D3D11CreateDevice` is rejecting the default call signature before the renderer even touches staging buffers.
- `.venv\Scripts\python.exe -c "from yup_rive_renderer import RiveOffscreenRenderer; ..."` reports `CreateTexture2D (staging) failed (0x8007000E): Not enough memory resources are available to complete this operation.` when requesting 640×360 with presentation disabled. The constructor throws before the diagnostics buffer can be retrieved, but the error string indicates the staging texture allocation is the first failure point after device creation.
- After mirroring adapter enumeration, rebuilding, and rerunning `tools\check_d3d11_device.py`, the script still reported `0x80070057` on earlier builds�`tmp_run_renderer.py` would raise `ValueError: bad allocation` before startup logging flushed. With the 2025-10-06 diagnostics rebuild the renderer now initialises on the RTX 5090; keep monitoring future runs in case the allocation failure resurfaces on other adapters.
- The renderer now logs each DXGI adapter discovered plus every `D3D11CreateDevice` attempt, including the HRESULT for debug and non-debug flag combinations. Capture this output via `get_diagnostics()` to confirm whether hardware or WARP creation is failing first on future runs.

## Latest Diagnostics (2025-10-06)
- Repeated ten-construction loops of `RiveOffscreenRenderer(1280, 720, staging_buffer_count=1, enable_presentation=False)` now succeed (`total failures: 0 / 10`), but the first construction in fresh Python processes still intermittently raises `ValueError: Failed to allocate staging buffers (bad allocation/unknown error)`. Captured diagnostics show the failure occurs before GPU resource creation.
- Rebuilding the editable Python package (`pip install -e python`) still fails: the build now completes the native targets but errors while copying `yup.cp311-win_amd64.pyd` into the editable wheel staging directory (the file never materialises). Investigate the wheel layout or adjust `CMAKE_RUNTIME_OUTPUT_DIRECTORY` before relying on editable installs.
- `dxcap.exe -forcetdr -file %TEMP%\rive_hw.etl -c ".venv\Scripts\python.exe python/examples/run_rive_ndi.py --name DebugCapture --width 1280 --height 720 --fps 60 --present-preview examples/graphics/data/alien.riv"` (and the matching `YUP_RIVE_FORCE_WARP=1` run) emitted ~4 KB ETL traces, but both runs aborted with `ValueError: Failed to allocate staging buffers (bad allocation)` before publishing frames.
- Running `python/examples/run_rive_ndi.py` with `--present-preview` (and `YUP_RIVE_ENABLE_D3D_DEBUG=1`) initialises the device on the RTX 5090, but the Direct3D debug layer reports `CreateUnorderedAccessView(...): The parameter is incorrect.` from `thirdparty/rive_renderer/source/d3d11/render_context_d3d_impl.cpp:99`, after which the process exits.
- Instantiating `RiveOffscreenRenderer(1280, 720, staging_buffer_count, enable_presentation)` directly shows offscreen-only paths currently succeed only when `staging_buffer_count` is exactly `3`; counts of `1`, `2`, or `4` raise the same staging-buffer allocation errors. Presentation-enabled paths accept those counts but still trip the UAV error above once rendering begins. Until the allocator is fixed, request `staging_buffer_count=3` when wiring the orchestrator.

## Rive Initialisation Reference
- See `renderer/src/d3d11/render_context_d3d_impl.cpp` inside the upstream Rive runtime (`C:\Users\AX-6\Documents\GitHub\rive-runtime`). It mirrors the pipeline our renderer wraps: `RenderContextD3DImpl::MakeContext` constructs the GPU context and allocates render targets before invoking Rive's tessellation shaders.
- The Rive D3D11 implementation expects feature level 11.0+ and relies on BGRA render targets, matching the settings logged by `RiveOffscreenRenderer`.
- When diagnosing Rive import failures, inspect `rive::File::import` and the `Artboard::defaultScene()` helper (both in the same repo) to ensure our wrapper discards invalid artboards early.

## Open Questions / Next Steps
- Investigate the UAV descriptor we pass to `gpu->CreateUnorderedAccessView` when presentation is enabled; capture the `D3D11_UNORDERED_ACCESS_VIEW_DESC` to explain the parameter mismatch.
- Decide whether to raise the default `staging_buffer_count` to `3` or fix the staging allocator so `1` works reliably before shipping the Windows pipeline.
- Capture `CreateDXGIFactory1` output on the RTX 5090 workstation to confirm which adapter index the renderer selects when the "bad allocation" error is raised.
- Use the new diagnostics report to record the HRESULT emitted before `ValueError: Failed to initialise RiveOffscreenRenderer: bad allocation` bubbles up, then correlate it with the D3D debug layer output in Visual Studio.
- Once the renderer constructs successfully, add an integration test that streams frames through `NDIOrchestrator` while asserting that `get_diagnostics()` remains empty during steady-state rendering.
- Stay aligned with the upstream `yup_constructDirect3DGraphicsContext` implementation (`modules/yup_graphics/native/yup_GraphicsContext_d3d.cpp` in `yup_upstream_ref`): our offscreen renderer now mirrors its adapter enumeration + `D3D_DRIVER_TYPE_UNKNOWN` device creation before falling back to driver-type calls. Monitor future HRESULTs to confirm the change eliminates the `0x80070057` path in the diagnostics tool.
## Diagnostics Update (2025-10-05)
- Ran the refreshed `tools/check_d3d11_device.py`; hardware and WARP drivers both initialise successfully at feature level 0x0000B000. Output:
  ```
  Enumerating adapters via IDXGIFactory1...
  EnumAdapters1(3) failed 0x887A0002 (The object was not found. If calling IDXGIFactory::EnumAdaptes, there is no adapter with the specified ordinal.)
  Adapter 0: NVIDIA GeForce RTX 5090 (vendor=0x10DE, device=0x2B85, VRAM=31.35 GiB)
    Success (flags=0): feature level 0x0000B000
  Adapter 1: NVIDIA GeForce RTX 5090 (vendor=0x10DE, device=0x2B85, VRAM=31.35 GiB)
    Success (flags=0): feature level 0x0000B000
  Adapter 2: Microsoft Basic Render Driver (vendor=0x1414, device=0x008C, VRAM=0.00 GiB)
    Success (flags=0): feature level 0x0000B000

  Attempting driver-type creation...
  Success: Direct3D11 device initialised using hardware driver
  Feature level: 0x0000B000
  ```
  Running with `--warp` mirrors the above but forces the WARP driver (same feature level output).
- The renderer now annotates diagnostics with adapter enumeration, flag selection, and environment overrides. Set `YUP_RIVE_FORCE_WARP=1` to skip hardware paths entirely, and `YUP_RIVE_ENABLE_D3D_DEBUG=1` to force the debug layer even on release builds. Both toggles are recorded in `get_diagnostics()`.
- Earlier builds still raised `ValueError: bad allocation` before the Direct3D device initialised; those notes remain for reference. The current diagnostics build (2025-10-06) succeeds on the RTX 5090, but we still need to capture HRESULTs if the failure reappears on other hardware.



## Startup Diagnostics Integration (2025-10-06)
- Added a thread-local startup log inside `RiveOffscreenRenderer`; every Direct3D step now records both to the JUCE logger and stderr prefixed with `[RiveStartup]`.
- Python bindings expose the buffer via `yup_rive_renderer._debug_consume_startup_diagnostics()` and report the active stage with `_debug_startup_stage()`; the constructor also injects the collected log when raising `ValueError`.
- Successful hardware initialisation on the RTX 5090 now produces verbose adapter/device output before reporting `Rive offscreen renderer ready (presentation disabled)`.
- Invalid dimension guardrails emit startup diagnostics too; Python callers receive the stage (`startup`) alongside the error and the renderer's diagnostics payload.

Example hardware run (Release build, `.venv` Python):
```
[RiveOffscreenRenderer] Constructing RiveOffscreenRenderer (1280x720, staging=2, presentation=disabled)
[RiveStartup] Initialising Direct3D11 offscreen renderer (1280x720, staging=2, presentation=disabled, frame=1440 bytes)
[RiveStartup] Adapter 0: NVIDIA GeForce RTX 5090 (vendor=0x10DE, device=0x2B85, VRAM=31.35 GiB, flags=0x00000000)
[RiveStartup] D3D11CreateDevice (NVIDIA GeForce RTX 5090, flags=0x00000020) succeeded (feature=11_1)
[RiveStartup] Rive GPU render context created
[RiveStartup] Rive offscreen renderer ready (presentation disabled)
```

Example failure (invalid width) showing surfaced diagnostics:
```
Renderer dimensions must be positive (received 0x720)
Diagnostics:
[error] Renderer dimensions must be positive (received 0x720)
Startup diagnostics:
Constructing RiveOffscreenRenderer (0x720, staging=2, presentation=disabled)
Constructing renderer Impl (0x720, staging=2, presentation=disabled)
Renderer dimensions must be positive (received 0x720)
Startup stage: startup
```
## Rive NDI Example Runs (2025-10-06)
- Hardware command: `.\.venv\Scripts\python.exe python/examples/run_rive_ndi.py --name TestHardware --width 1280 --height 720 --fps 60 --present-preview examples/graphics/data/alien.riv`.
  - Renderer initialised successfully on adapter 0 (RTX 5090, feature level 11_1) with presentation enabled, then failed when the Rive D3D11 runtime attempted to create the UAV backing the render context:
    ```
    Rive offscreen renderer ready (presentation enabled)
    Loading Rive file '...alien.riv' (68315 bytes)
    Loaded Rive file with 1 artboard(s)
    thirdparty/rive_renderer/source/d3d11/render_context_d3d_impl.cpp:99:
      D3D error The parameter is incorrect.: gpu->CreateUnorderedAccessView(tex, &uavDesc, uav.ReleaseAndGetAddressOf())
    ```
  - The `_debug_*` buffer could not be sampled after the failure because the underlying Rive assertion terminates the process before Python regains control. Capture Visual Studio debug-layer output next to pinpoint which UAV flags are missing.
- WARP command: `set YUP_RIVE_FORCE_WARP=1` followed by the same CLI invocation.
  - WARP device creation succeeds at feature level 11_1 and hits the identical UAV error during `RenderContextD3DImpl` startup, confirming the issue is independent of the hardware driver.
- Headless (`--present-preview` omitted) exhibits the same UAV failure, so the swap-chain path is not the trigger.

Action: enable the D3D debug layer (`YUP_RIVE_ENABLE_D3D_DEBUG=1`) and gather HRESULT details for the UAV creation call, then audit the UAV descriptor against Rive's expectations (ALLOW_UNORDERED_ACCESS + typed UAV formats).
## Direct3D Debug Layer Experiments (2025-10-06)
- Hardware + debug layer: `.\.venv\Scripts\python.exe python/examples/run_rive_ndi.py --name TestDebugHW --width 1280 --height 720 --fps 60 --present-preview examples/graphics/data/alien.riv` with `YUP_RIVE_ENABLE_D3D_DEBUG=1`.
  - One run failed before device creation with `Failed to allocate staging buffers (bad allocation)`; the startup log recorded only the staging allocation phase, implying the default 1280×720×BGRA allocation path can trip when the debug layer is enabled. A direct constructor call immediately afterwards succeeded, so this may be allocator pressure from the surrounding orchestrator setup. Capture heap stats if it repeats.
  - When the constructor succeeds, the subsequent load still triggers the UAV error noted below; no additional debug output was emitted to stdout, so we need to collect the ID3D11 debug messages via Visual Studio/DebugView next.
- WARP + debug layer: `set YUP_RIVE_FORCE_WARP=1`, `set YUP_RIVE_ENABLE_D3D_DEBUG=1`, same CLI invocation.
  - Device creation succeeds with flags `0x00000022` (debug + BGRA) and the run again ends at `gpu->CreateUnorderedAccessView` with `E_INVALIDARG`, confirming the UAV descriptor mismatch is independent of the debug layer and driver type.

Next action: attach the process to the D3D debug output (e.g. Visual Studio Graphics Diagnostics) to capture the exact validation message for the UAV creation failure so we can compare it against the descriptor we supply in `RenderContextD3DImpl`.
## DXGI Debug-Layer Capture Plan (Pending)
- Capture the failing run under the Direct3D debug layer to see why `CreateUnorderedAccessView` is returning `E_INVALIDARG`.
  1. Launch Visual Studio in `Release|x64`, open `build\yup.sln`, and use `Debug → Graphics → Graphics Diagnostics…` to set a custom launch target.
     - Executable: `.venv\Scripts\python.exe`
     - Working directory: `C:\Users\AX-6\Documents\GitHub\yup`
     - Arguments: `python/examples/run_rive_ndi.py --name DebugCapture --width 1280 --height 720 --fps 60 --present-preview examples/graphics/data/alien.riv`
     - Environment: `YUP_RIVE_ENABLE_D3D_DEBUG=1` (add `YUP_RIVE_FORCE_WARP=1` for a WARP pass).
     - Disable auto-build before launching to avoid the failing Debug configuration.
  2. Click Start Diagnostics, let the UAV failure occur, and inspect the captured `.vsglog` (Messages view) for the exact debug-layer warning.
  3. If the UI cannot be reached, use `dxcap.exe -forcetdr` with `cmd.exe /c` to wrap the same command, then open the generated `.etl` in Visual Studio to extract the messages.
  4. Share the debug-layer output so we can reconcile the UAV descriptor with Rive’s expectations.

Current blockers: Graphics Diagnostics/dxcap require manual setup on the host, and we can’t trigger or configure them programmatically from this session.
