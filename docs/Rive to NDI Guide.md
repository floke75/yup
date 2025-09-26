# Rive → NDI Pipeline Guide

This document describes the end-to-end workflow for streaming Rive animations to NDI using the YUP
renderer, Python bindings, and orchestration utilities.

> [!NOTE]
> When configuring with `-DYUP_ENABLE_AUDIO_MODULES=OFF`, YUP skips the audio-dependent console, app,
> graphics, and plugin samples as well as the CTest suite. This keeps the slimmed-down Rive → NDI
> workflow free from audio build requirements.
>
> [!TIP]
> Python wheels honour the same toggle. Set `YUP_ENABLE_AUDIO_MODULES=0` in your environment before
> running `python -m build python` (or `pip wheel`) to publish artifacts that exclude the audio stack.
> Switch it back to `1` if a consumer explicitly needs the legacy audio APIs.

## 1. Focus the Build
The renderer still depends on targeted helpers from `yup_core`, `yup_events`, and `yup_graphics`.
Before deleting or renaming those modules, confirm that `modules/yup_gui/artboard` remains buildable
and that the pybind11 bindings compile. Use the following CMake invocation to configure Visual Studio
projects that prioritise the renderer, bindings, and tests:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
  -DYUP_ENABLE_AUDIO_MODULES=OFF \
  -DYUP_BUILD_TESTS=ON \
  -DYUP_BUILD_EXAMPLES=OFF
cmake --build build --config Release
```

## 2. Build & Install the Python Packages
Run the `just` recipe to build the wheel, reinstall it into the current environment, and execute the
Python unit tests:

```powershell
just python_wheel
```

Alternatively, run the underlying commands manually from the `python/` directory:

```powershell
python -m build --wheel
python -m pip install --force-reinstall dist/yup-*.whl
pytest -q
```

## 3. Drive the Renderer from Python
The `yup_rive_renderer` module mirrors every method exposed by
`modules/yup_gui/artboard/yup_RiveOffscreenRenderer.*`. A minimal usage example:

```python
from yup_rive_renderer import RiveOffscreenRenderer

renderer = RiveOffscreenRenderer(width=1920, height=1080, staging_buffer_count=3)
if not renderer.is_valid():
    raise RuntimeError(renderer.get_last_error())

renderer.load_file("assets/demo.riv", artboard="Main")
print(renderer.list_animations())  # discover scenes
renderer.play_animation("Intro", loop=False)
progressed = renderer.advance(1 / 60)
frame = renderer.acquire_frame_view()  # zero-copy BGRA memoryview
```

`staging_buffer_count` controls how many GPU readback textures the renderer keeps in flight. Increase
the value (for example, when feeding multiple consumers) to smooth out bursts at the cost of a few
frames of additional latency. Leave it at the default of `1` when you need lowest-latency updates.

`acquire_frame_view()` returns a read-only `memoryview` that can be cast to a flat byte buffer or into
`(height, width, 4)` NumPy arrays. `get_frame_bytes()` remains available when a defensive copy is
needed.

## 4. Publish Frames Over NDI
The `yup_ndi` package orchestrates renderers and senders. It accepts factories for dependency
injection during testing, but defaults to constructing `RiveOffscreenRenderer` instances and
`cyndilib` senders. Example:

```python
from fractions import Fraction
from yup_ndi import NDIOrchestrator, NDIStreamConfig

with NDIOrchestrator() as orchestrator:
    orchestrator.add_stream(
        NDIStreamConfig(
            name="StudioA",
            width=1920,
            height=1080,
            riv_path="assets/demo.riv",
            animation="Loop",
            loop_animation=True,
            frame_rate=Fraction(60000, 1001),
            ndi_groups="ControlRoom",
            metadata={"ndi": {"comment": "Rive playback"}},
            renderer_options={"staging_buffer_count": 3},
        )
    )

    for _ in range(600):
        orchestrator.advance_all(1 / 60)
```

Use `apply_stream_control()` to pause/resume playback, select artboards, or set state-machine inputs
at runtime. Register custom control handlers via `register_control_handler()` if you expose REST/OSC
interfaces above the orchestrator.

> [!NOTE]
> `NDIStreamConfig.frame_rate` accepts either a :class:`fractions.Fraction`, a float, or a
> `(numerator, denominator)` tuple. The value is normalised to a positive Fraction internally; passing
> `0` (or `None`) disables deterministic cadence and defers to wall-clock timing. Tuples must use a
> non-zero denominator—validation errors are raised eagerly so misconfigured streams fail fast.

> [!IMPORTANT]
> When you configure a `frame_rate`, use `NDIOrchestrator.set_stream_start_time()` (or pass the
> optional `start_time` keyword to `add_stream()`) to anchor the deterministic 100 ns timeline before
> sending the first frame. This prevents drift when pumping fractional rates such as 30000/1001. The
> CLI's frame pump captures a monotonic start time and primes the stream automatically.

`renderer_options` accepts a `staging_buffer_count` entry that forwards to the renderer constructor,
mirroring the direct Python API. Set it to a higher value when streams are consumed asynchronously or
when a few extra milliseconds of buffering keeps throughput stable. Invalid or missing entries fall
back to the default of `1` and raise descriptive errors when misconfigured.

### Command-line runner and control surfaces
The package now ships with a convenience CLI so you can spin up a stream without writing a custom
script. Invoke it with `python -m yup_ndi` and supply the renderer dimensions plus the `.riv` file:

```powershell
python -m yup_ndi --name StudioA --riv-file assets/demo.riv --width 1920 --height 1080 \
    --animation Loop --ndi-groups ControlRoom --fps 60000/1001 --rest-port 5000 --osc-port 5001
```

`--fps` accepts either decimal values (e.g. `59.94`) or exact ratios such as `60000/1001` so you
can preserve broadcast frame cadences when configuring NDI senders. Passing `0` keeps the renderer
in wall-clock mode, matching the CLI's legacy behaviour.

The CLI supports the same configuration payload as `NDIStreamConfig`, including `--state-input`
pairs, connection throttling toggles, and optional REST/OSC servers:

- `--rest-port` spins up a Flask server that exposes `/streams`, `/streams/<name>`,
  and `/streams/<name>/control` endpoints for remote automation.
- `--osc-port` enables an OSC listener (requires `python-osc>=1.8`) that accepts
  `/<namespace>/<stream>/control` messages and responds to
  `/<namespace>/<stream>/metrics` requests with a JSON payload describing frame
  counters, connection counts, and pause state.
- `--no-throttle` and `--pause-when-inactive` toggle the connection-aware behaviour described
  below.

### Connection-aware throttling
`NDIStreamConfig` exposes three new fields to help conserve GPU/CPU time when nobody is watching:

- `throttle_when_inactive` (default `True`) skips NDI uploads when `cyndilib` reports zero receivers.
- `pause_when_inactive` pauses the renderer entirely while inactive so animations resume from where
  they left off when a listener connects.
- `inactive_connection_poll_interval` controls how often the orchestrator polls the sender for
  updated connection counts.

Runtime statistics are surfaced through `NDIStreamMetrics` and the CLI's periodic logging so you can
track how many frames were sent or suppressed, whether throttling is active, and the most recent
connection count.

## 5. Keep Tests Green
The smoke tests in `python/tests/` validate both the binding surface and the orchestrator:

```powershell
just python_smoke
```

The suites provide fake renderers and senders so they run without native GPU or NDI dependencies.
When modifying the renderer API, update the bindings and tests in the same commit to avoid drift.

## 6. Troubleshooting
- **`ImportError: yup_rive_renderer`:** Ensure `python -m build --wheel` succeeded and that the wheel
  was installed into the active Python environment.
- **`ImportError: cyndilib`:** Install `cyndilib>=0.0.8` when streaming to real NDI receivers. The
  orchestrator tests work without it because they inject fake senders.
- **Black frames or zero-sized memory views:** Confirm `advance()` is called with a positive delta and
  that the renderer reported a valid artboard/animation. Use `get_last_error()` for detailed GPU
  diagnostics.
