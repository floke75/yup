# Rive → NDI Pipeline Overview (Windows)

This guide summarises the Windows-focused workflow for turning Rive animations into NDI video streams
with YUP. The renderer, Python bindings, and orchestrator are fully implemented; this document links
those components together and highlights the build/test steps that keep them aligned.

## What Ships Today
- **Headless Direct3D 11 rendering:** `yup::RiveOffscreenRenderer` initialises a swapchain-free D3D11
device, renders Rive artboards into BGRA textures, and exposes deterministic CPU readback with a
configurable staging-buffer ring to balance latency against throughput.
- **Python binding surface:** The `yup_rive_renderer` module mirrors the renderer API, including
zero-copy frame views that the orchestrator can forward directly to NDI senders.
- **NDI orchestration:** The `yup_ndi` package manages multiple renderers, maintains timing metadata,
forwards frames to `cyndilib` senders, and provides runtime control hooks.
- **Focused test coverage:** GoogleTests validate renderer invariants while `pytest` suites exercise
the binding and orchestrator behaviour using fake senders/renderers so CI does not require GPU or NDI
DLLs.

## Key Source Locations
| Component | Purpose | Location |
| --- | --- | --- |
| Offscreen renderer core | Direct3D 11 setup, artboard control, BGRA readback. | `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.*` |
| Python binding layer | pybind11 module that exposes renderer APIs and memory views. | `python/src/yup_rive_renderer.cpp` |
| NDI orchestrator | Coordinates renderers and NDI senders, including metadata/control plumbing. | `python/yup_ndi/orchestrator.py` |
| Tests | Renderer GoogleTests plus Python binding/orchestrator suites. | `tests/yup_gui/`, `python/tests/` |

## How the Pieces Fit Together
1. **Render Frames:** `RiveOffscreenRenderer` manages the GPU context, loads `.riv` assets, and renders
into an offscreen BGRA texture.
2. **Expose to Python:** The `yup_rive_renderer` extension loads files, advances scenes, and returns
frames as bytes or `memoryview` objects without copying when possible. Callers can set
`staging_buffer_count` to control how many readback textures the renderer cycles through before data is
reused.
3. **Publish over NDI:** `yup_ndi.NDIOrchestrator` instantiates renderers, maps timestamps into the
100 ns NDI domain, forwards frames to `cyndilib` senders, and applies metadata/control commands. Use
`set_stream_start_time()` (or the optional `start_time` argument on `add_stream()`) to prime the
deterministic timestamp anchor when driving fractional frame rates so frames align to a consistent
monotonic origin. The CLI frame pump wires this up automatically before emitting the first frame.

The orchestrator also exposes a `renderer_options` dictionary so deployments can request deeper staging
queues when buffering bursts or multiple consumers is preferable to the lowest possible latency.

The pipeline is designed so that Python orchestrates playback while the renderer handles all GPU
work. Any API adjustment in the renderer must be mirrored in the binding and orchestrator to keep the
tests green.

## Recommended Build Configuration (Windows)
Use the following flags when generating Visual Studio projects that target the Rive → NDI workflow:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
  -DYUP_ENABLE_AUDIO_MODULES=OFF \
  -DYUP_BUILD_TESTS=ON \
  -DYUP_BUILD_EXAMPLES=OFF
```

Disable the legacy audio modules to shorten build times; tests remain available for the renderer and
Python components. Invoke `cmake --build build --config Release` (or `Debug`) to compile the native
targets.

### Python Wheel Builds
The Python wheel embeds the renderer bindings and orchestrator:

```powershell
just python_wheel
```

The recipe uses `python -m build --wheel`, reinstalls the package, and runs the Python tests. Set the
`YUP_ENABLE_AUDIO_MODULES=0` environment variable before invoking the build if you want wheels that
exclude audio modules entirely.

### Smoke Tests
`just python_smoke` executes:
- `python/tests/test_yup_rive_renderer/test_binding_interface.py`
- `python/tests/test_yup_ndi/test_orchestrator.py`

Both suites rely on in-repo fakes, so they run without a GPU or NDI runtime. Use them after any change
that touches the renderer interface, binding layer, or orchestrator.

## Next Steps for Contributors
- Continue refining documentation and tooling so Windows developers can provision environments quickly.
- Plan integration smoke tests that combine the real renderer with `cyndilib` senders for manual NDI
verification on hardware.
- Profile throughput to confirm the zero-copy pathway meets frame-rate targets for production
workloads.
