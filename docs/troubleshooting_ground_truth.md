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
- After mirroring adapter enumeration, rebuilding, and rerunning `tools\check_d3d11_device.py`, the script still reports `0x80070057` for both hardware and WARP. Launching `tmp_run_renderer.py` via the project venv continues to raise `ValueError: bad allocation` with no additional console diagnostics, implying the failure still occurs before the new logging path completes.
- The renderer now logs each DXGI adapter discovered plus every `D3D11CreateDevice` attempt, including the HRESULT for debug and non-debug flag combinations. Capture this output via `get_diagnostics()` to confirm whether hardware or WARP creation is failing first on future runs.

## Rive Initialisation Reference
- See `renderer/src/d3d11/render_context_d3d_impl.cpp` inside the upstream Rive runtime (`C:\Users\AX-6\Documents\GitHub\rive-runtime`). It mirrors the pipeline our renderer wraps: `RenderContextD3DImpl::MakeContext` constructs the GPU context and allocates render targets before invoking Rive's tessellation shaders.
- The Rive D3D11 implementation expects feature level 11.0+ and relies on BGRA render targets, matching the settings logged by `RiveOffscreenRenderer`.
- When diagnosing Rive import failures, inspect `rive::File::import` and the `Artboard::defaultScene()` helper (both in the same repo) to ensure our wrapper discards invalid artboards early.

## Open Questions / Next Steps
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
- Despite the improved logging, constructing `RiveOffscreenRenderer(1280x720)` still raises `ValueError: bad allocation` before the Direct3D device is created. The failure now captures an `[info] Initialising Direct3D11 offscreen renderer...` diagnostic entry internally, but the constructor throws before Python can surface it. The next follow-up should capture those diagnostics via the debug logger or by instrumenting the binding to return them alongside the exception.



