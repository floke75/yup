# Windows Rive + NDI build overview

The `rive_ndi_win` recipe in the root `justfile` streamlines configuring and compiling the Visual Studio 2022 solution that powers the Direct3D 11 Rive renderer and the Python wheel used for NDI streaming.

## Configure and build in one step

```bash
just rive_ndi_win
```

This command performs two actions:

1. Runs `cmake -G "Visual Studio 17 2022" -B build/rive_ndi -DYUP_ENABLE_AUDIO_MODULES=OFF -DYUP_BUILD_WHEEL=ON` to generate a dedicated build tree with audio modules disabled and the Python wheel enabled.
2. Invokes `cmake --build build/rive_ndi --target yup --config Release` so the renderer + bindings are ready without opening Visual Studio manually.

Pass `CONFIG=Debug` (or another multi-config preset) to request a different build configuration in the same command, e.g. `just rive_ndi_win CONFIG=Debug`. The build directory layout keeps the NDI workflow isolated from any default `build` artifacts while enabling you to open `build/rive_ndi/yup.sln` in Visual Studio for further work if desired.

## Follow-up steps

* **Iterate on code changes:** Re-run `cmake --build build/rive_ndi --target yup --config Release` (adjusting `--config` if you chose a different variant) after editing C++ or binding sources to rebuild quickly.
* **Run focused tests:** Once the Python wheel is produced you can execute Windows-side validation such as `py -m pytest` from the `python` folder (after installing the generated wheel) to confirm bindings behave as expected.
* **Package or publish:** When you need an updated wheel artifact, run `cmake --build build/rive_ndi --target yup --config Release` followed by `just python_wheel` (from the repo root) to regenerate and install/test the wheel using the same configuration.
* **Re-enable audio later:** If you need the full audio toolchain again, reconfigure with `-DYUP_ENABLE_AUDIO_MODULES=ON` (or omit the override entirely) and rerun the build command.

By consistently using `build/rive_ndi` you avoid cross-contaminating other build flavours while keeping the Direct3D/NDI toolchain reproducible.
