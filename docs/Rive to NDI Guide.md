# Rive → NDI Pipeline Guide (Preview)

This guide documents the concrete workflow for streaming Rive animations to NDI using the Windows-focused YUP toolchain.

> [!NOTE]
> When configuring with `-DYUP_ENABLE_AUDIO_MODULES=OFF`, YUP automatically skips the audio-dependent console, app, graphics, and plugin samples as well as the CTest suite. This keeps the slimmed-down Rive→NDI workflow free from audio build requirements.

> [!TIP]
> Python wheels honour the same toggle. Set `YUP_ENABLE_AUDIO_MODULES=0` in your environment before running `python -m build python` (or `pip wheel`) to publish artifacts that exclude the audio stack. Switch it back to `1` if a consumer explicitly needs the legacy audio APIs.

## 1. Build the Direct3D 11 renderer (Windows 11)

The offscreen renderer implementation lives in `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.h/.cpp`. Configure and build only the graphics-focused targets with MSVC 2022:

```powershell
cmake -S . -B build/rive-ndi-win `
  -G "Visual Studio 17 2022" `
  -DYUP_ENABLE_AUDIO_MODULES=OFF `
  -DYUP_ENABLE_PLUGIN_MODULES=OFF `
  -DYUP_ENABLE_EXAMPLES=OFF

cmake --build build/rive-ndi-win --target yup_gui --config RelWithDebInfo
```

This generates the Direct3D renderer used by both the C++ tests (`tests/yup_gui/yup_RiveOffscreenRenderer.cpp`) and the Python binding.

## 2. Compile the Python bindings and wheel

The pybind11 module lives at `python/src/yup_rive_renderer.cpp` with build glue in `python/CMakeLists.txt` and packaging metadata in `python/pyproject.toml`.

```powershell
cmake -S python -B build/rive-ndi-win-python `
  -G "Visual Studio 17 2022" `
  -DYUP_ENABLE_AUDIO_MODULES=OFF

cmake --build build/rive-ndi-win-python --target yup_rive_renderer --config RelWithDebInfo
cmake --build build/rive-ndi-win-python --target yup --config RelWithDebInfo

cd python
python -m build --wheel
python -m pip install --force-reinstall dist/yup-*.whl
cd ..
```

Installing the freshly built wheel makes the `yup_rive_renderer` module importable for the orchestration tests.

## 3. Run the renderer ↔︎ NDI smoke tests

Targeted pytest coverage now verifies the binding, frame acquisition, and orchestration logic:

```powershell
just python_test_rive_ndi
```

This recipe runs the suites under `python/tests/test_yup_rive_renderer/` (binding construction, zero-copy frame views) and `python/tests/test_yup_ndi/` (mocked renderers + NDI senders, timestamp mapping, and control flows). Each test is guarded to skip gracefully if the native modules are unavailable on non-Windows hosts.

## 4. Launch orchestrated streams

With the wheel installed, integrate the orchestrator from `python/yup_ndi/orchestrator.py` into your automation tooling. The entry points under `python/yup_ndi/__init__.py` expose `NDIOrchestrator` and `NDIStreamConfig`. Example usage:

```python
from yup_ndi import NDIOrchestrator, NDIStreamConfig

config = NDIStreamConfig(
    name="showcase",
    width=1920,
    height=1080,
    riv_path=r"C:\\assets\\title.riv",
    animation="intro",
    metadata={"ndi": {"scene": "title"}},
)

with NDIOrchestrator() as orchestrator:
    orchestrator.add_stream(config)
    orchestrator.advance_stream("showcase", 1 / 60)
```

The orchestrator accepts optional REST/OSC control hooks and multiple concurrent streams; see the in-line documentation for the `NDIStreamConfig` dataclass and the `_NDIStream` helper.

## Reference locations

| Component | Files |
| --- | --- |
| Renderer implementation | `modules/yup_gui/artboard/yup_RiveOffscreenRenderer.h/.cpp` |
| Renderer unit tests | `tests/yup_gui/yup_RiveOffscreenRenderer.cpp` |
| Python binding | `python/src/yup_rive_renderer.cpp` + `python/CMakeLists.txt` |
| Python orchestration | `python/yup_ndi/orchestrator.py` + `python/yup_ndi/__init__.py` |
| Python smoke tests | `python/tests/test_yup_rive_renderer/`, `python/tests/test_yup_ndi/` |

By following the sequence above you can build, package, and validate the Windows-only pipeline without touching unrelated modules.
