# Windows Build and Packaging Workflow

This guide captures the end-to-end workflow for building the Direct3D11 Rive renderer,
packaging the Python bindings, and validating the NDI orchestration path on Windows 11.
The steps assume Visual Studio 2022 (or another C++20-compatible compiler), Python 3.11+,
and the Windows 10 or 11 SDK are installed.

## 0. Automated bootstrap (optional)

When you want a turnkey setup, run the PowerShell helper from a VS 2022 developer
prompt:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
./tools/install_windows.ps1
```

The script creates (or reuses) `.venv`, installs build/test dependencies,
configures the Visual Studio solution with audio modules disabled, builds the
specified configuration (Release by default), produces the Python wheel,
reinstalls it into the virtual environment, and runs the renderer/NDI smoke
tests. Use `-Configuration Debug`, `-SkipWheel`, `-SkipSmokeTests`, or
`-InstallCyndilib` to adjust the workflow.

## 1. Prepare the environment

1. Launch a **x64 Native Tools Command Prompt for VS 2022** so that MSVC, the Windows SDK,
   and CMake are all on the `PATH`.
2. Install Python dependencies into a clean virtual environment:

   ```powershell
   py -3.11 -m venv .venv
   .venv\Scripts\Activate.ps1
   python -m pip install --upgrade pip cmake ninja build pytest
   ```
3. Install the optional runtime dependencies that unlock the smoke tests:

   ```powershell
   python -m pip install cyndilib==0.0.8
   ```

## 2. Configure and build the native renderer

1. Configure the project with Visual Studio 2022 generators. Disable the legacy audio
   modules to shorten build times while keeping the renderer, bindings, and tests available.

   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
     -DYUP_ENABLE_AUDIO_MODULES=OFF \
     -DYUP_BUILD_TESTS=ON \
     -DYUP_BUILD_EXAMPLES=OFF
   ```

2. Build the desired configuration:

   ```powershell
   cmake --build build --config Release --target ALL_BUILD
   ```

3. (Optional) Build the Debug configuration when preparing symbols for investigation:

   ```powershell
   cmake --build build --config Debug --target ALL_BUILD
   ```

## 3. Build and install the Python wheel

1. From the repository root, build the wheel, reinstall it into the active virtual
   environment, and run the Python unit tests:

   ```powershell
   just python_wheel
   ```

   The recipe runs `python -m build --wheel`, reinstalls the freshly built package, and
   executes the Python unit tests. Export `YUP_ENABLE_AUDIO_MODULES=0` before invoking the
   command if you want an audio-free wheel.

2. To build only the wheel without reinstalling it, run the underlying command directly:

   ```powershell
   pushd python
   python -m build --wheel
   popd
   ```

## 4. Run renderer and NDI smoke tests

The `python/tests/test_yup_rive_renderer` and `python/tests/test_yup_ndi` suites validate
that the bindings expose zero-copy frame access and that the orchestrator can marshal
frames into NDI senders. A convenient `just` recipe is available:

```powershell
just python_smoke
```

The command executes the targeted smoke tests with `-q` so that any failures surface
immediately. The tests ship with fake renderer/sender implementations, so they succeed even
when GPU or NDI runtimes are absent.

## 5. Package distributables

1. Collect the wheel(s) produced under `python/dist/`.
2. Bundle the NDI runtime redistributables that the production pipeline requires. NewTek's
   redistributable installer must be shipped separately when distributing to third parties.
3. If you need to distribute the native binaries alongside the Python bindings, stage the
   relevant `yup_rive_renderer.pyd` from `python/.venv/Lib/site-packages` (or your chosen
   install prefix) together with any dependent DLLs produced in `build/<config>/`.
4. Archive the build artefacts and documentation so that downstream consumers receive the
   renderer, wheel, and guidance in a single package.

## 6. Continuous integration notes

- The Windows smoke tests expect GPU-less environments and operate entirely through the
  fake renderer/sender scaffolding. They do not require an attached display.
- When automating wheel builds, ensure that the Visual Studio environment variables are set
  before invoking `just python_wheel` or `python -m build`.
- Cache the `build/` and `.venv/` folders between CI runs to avoid repeated CMake
  configuration and dependency installation costs.

## Troubleshooting

- **`'sleep': identifier not found` during the audio device build** – The Windows toolchain
  does not provide the POSIX `sleep()` symbol, so any backend that still calls it will fail
  to compile. Replace raw `sleep()`/`Sleep()` usages in platform code (e.g. the DirectSound
  backend) with `Thread::sleep(milliseconds)` so the implementation maps cleanly onto the
  Windows API.
