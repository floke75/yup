# Windows Build and Packaging Workflow

This guide captures the end-to-end workflow for building the Direct3D 11 Rive renderer,
packaging the Python bindings, and validating the NDI orchestration path on Windows 11.
Follow it when you want repeatable "clone → build → smoke test" instructions that match
the latest toolchain and script automation.

## Before you start

Make sure these prerequisites are installed **before** you open the developer prompt:

| Requirement | Notes |
| --- | --- |
| Visual Studio 2022 (Desktop development with C++) | Install the workload together with the Windows 10/11 SDK, MSVC v143 build tools, CMake tools for Windows, and the ATL headers. |
| CMake 3.28+ and Ninja 1.11+ | Bundled versions in Visual Studio are often older. Install current builds from [cmake.org](https://cmake.org/download/) and [ninja-build.org](https://github.com/ninja-build/ninja/releases). |
| Python 3.11 (or newer 3.10+) | Check **Add python.exe to PATH** during setup. Verify with `python --version`. |
| Git for Windows | Required when cloning the repository with submodules. |
| (Optional) [`just`](https://github.com/casey/just) 1.21+ | The docs reference recipes such as `just python_wheel`. Install from the [GitHub releases](https://github.com/casey/just/releases), via `winget install --id Casey.Just`, or `cargo install just`. |
| (Optional) `cyndilib==0.0.8` | Enables live NDI transmission smoke tests. The helper script can install it for you. |
| (Optional) `vcpkg` with `libjpeg-turbo:x64-windows` | Required only when decoding JPEG textures in renderer smoke tests. |

> [!IMPORTANT]
> This branch intentionally supports **desktop Windows builds only**. The CMake tooling now
> hard-errors when configured on macOS, Linux, UWP, or other non-Windows targets so that the
> engineering effort stays focused on the Direct3D 11 → Python → NDI pipeline. If you need
> multiplatform support, use an earlier commit before the Windows-only guard was introduced
> or track the upstream repository for future cross-platform updates.

## 0. Automated bootstrap (optional)

When you want a turnkey setup, run the PowerShell helper from a VS 2022 developer
prompt after cloning the repository (`git clone --recurse-submodules https://github.com/kunitoki/yup.git`):

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
./tools/install_windows.ps1
```

The script creates (or reuses) `.venv`, installs build/test dependencies,
configures the Visual Studio solution with audio modules disabled, builds the
specified configuration (Release by default), produces the Python wheel,
reinstalls it into the virtual environment, and runs the renderer/NDI smoke
tests that cover the binding, orchestrator, and CLI layers. Use
`-Configuration Debug`, `-SkipWheel`, `-SkipSmokeTests`, or `-InstallCyndilib`
to adjust the workflow. Point `-PythonExecutable` at a specific interpreter if
`py -3.11` is unavailable.

Behind the scenes the helper opts into `YUP_BUILD_WHEEL=ON` while turning off `YUP_EXPORT_MODULES` so only the Python-focused targets are configured. That combination ensures the renderer module drops its `.pyd` next to `setup.py`, ready for packaging. Make sure the interpreter that runs `tools/package_wheel.py` has `python -m build` available (the bootstrap installs it inside `.venv`).

## 1. Prepare the environment

1. Launch a **x64 Native Tools Command Prompt for VS 2022** so that MSVC, the Windows SDK,
   and CMake are all on the `PATH`. Confirm the key tools respond as expected:

   ```powershell
   cl
   cmake --version
   ninja --version
   python --version
   just --version  # optional
   ```

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

4. Install the JPEG raster dependency if you plan to decode `.jpeg` assets. The build now
   consumes `libjpeg-turbo`'s CMake config automatically, so installing the vcpkg port is
   sufficient. Run this only when `vcpkg` is configured on your machine:

   ```powershell
   vcpkg install libjpeg-turbo:x64-windows
   ```

## 2. Configure and build the native renderer

1. Configure the project with Visual Studio 2022 generators.
> [!NOTE]
> Use **Visual Studio 2022 (v143 toolset)**. Earlier VS releases (2019 and below) are no longer validated for this branch and may fail to compile the modern C++17/C++20 configuration expected by the renderer and Python bindings.

> [!TIP]
> When building inside a virtual environment, pass `-DPython_EXECUTABLE=%CD%\.venv\Scripts\python.exe` during the CMake
> configure step so the renderer and bindings target the same interpreter you use to run tests. The system Python
> may otherwise be selected (for example, Python 3.13), producing wheels that fail to import under Python 3.11.

Disable the legacy audio
   modules to shorten build times while keeping the renderer, bindings, and tests available.
   Run the command from the repository root (next to `CMakeLists.txt`):

   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 \
     -DYUP_ENABLE_AUDIO_MODULES=OFF \
     -DYUP_BUILD_TESTS=ON \
     -DYUP_BUILD_EXAMPLES=OFF
   ```

   > [!NOTE]
   > The module graph now builds as actual **static libraries** when sources are present.
   > CMake performs the compilation once per module and wires dependencies through
   > `target_link_libraries`, so Windows configurations no longer need to re-list every
   > third-party include directory or define when adding new consumers of `yup_gui`,
   > `yup_python`, or the Rive decoders.

   > [!TIP]
   > SDL2 is now fetched automatically during configuration on Windows, so no manual SDK installation is required.

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
   environment, and run the Python unit tests. Install `just` if you want to use the
   convenience recipe (or invoke the underlying commands manually):

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

The focused smoke set runs `python/tests/test_yup_rive_renderer/test_binding_interface.py`,
`python/tests/test_yup_ndi/test_orchestrator.py`, and
`python/tests/test_yup_ndi/test_cli.py` to validate zero-copy frame access, orchestrator
NDI marshalling, and CLI parsing. A convenient `just` recipe is available:

```powershell
just python_smoke
```

The command executes the targeted smoke tests with `-q` so that any failures surface
immediately. The tests ship with fake renderer/sender implementations, so they succeed even
when GPU or NDI runtimes are absent. To run the tests manually, invoke `python -m pytest`
with the same paths listed in the recipe.

## 5. Package distributables

1. Collect the wheel(s) produced under `python/dist/`.
2. Bundle the NDI runtime redistributables that the production pipeline requires. NewTek's
   redistributable installer must be shipped separately when distributing to third parties.
3. If you need to distribute the native binaries alongside the Python bindings, stage the
   relevant `yup_rive_renderer.pyd` from `python/.venv/Lib/site-packages` (or your chosen
   install prefix) together with any dependent DLLs produced in `build/<config>/`.
4. Archive the build artefacts and documentation so that downstream consumers receive the
   renderer, wheel, and guidance in a single package.

The wheel now ships two native entry points: `yup` (the monolithic extension exposed through `setuptools.Extension`) and the renderer-specific `yup_rive_renderer` which is copied in as packaged data. Consumers can either `import yup` for the legacy surface or `import yup_rive_renderer` for the focused Direct3D 11 binding that feeds NDI.

## 6. Continuous integration notes

- The Windows smoke tests expect GPU-less environments and operate entirely through the
  fake renderer/sender scaffolding. They do not require an attached display.
- When automating wheel builds, ensure that the Visual Studio environment variables are set
  before invoking `just python_wheel` or `python -m build`.
- Cache the `build/` and `.venv/` folders between CI runs to avoid repeated CMake
  configuration and dependency installation costs.

## Troubleshooting

- **`python tools/package_wheel.py` fails with `No module named build`** - Activate the project's virtual environment before running the helper so the `build` package is on `PATH`. If you prefer a global interpreter, install it manually (`python -m pip install build`) or call the script through `py -3.11 -m tools.package_wheel`.

- **`stubgen` is not on PATH during wheel builds** - Type stubs are generated automatically now that the renderer artefacts land beside `setup.py`. Install `mypy` in the active environment or suppress stub generation with `set YUP_GENERATE_PYI=0` when diagnostics outweigh the benefits.

- **Release build emits only `.lib` artefacts** - Confirm that CMake is configured with `-DYUP_BUILD_WHEEL=ON` and `-DYUP_EXPORT_MODULES=OFF`. Those switches restrict the graph to the Python-facing modules and force `yup_rive_renderer.pyd` into the packaging directory.

- **`'sleep': identifier not found` during the audio device build** – The Windows toolchain
  does not provide the POSIX `sleep()` symbol, so any backend that still calls it will fail
  to compile. Replace raw `sleep()`/`Sleep()` usages in platform code (e.g. the DirectSound
  backend) with `Thread::sleep(milliseconds)` so the implementation maps cleanly onto the
  Windows API.
- **Direct3D 11 initialisation fails with `ValueError: Failed to allocate staging buffers`** � Track adapter information via `tools\check_d3d11_device.py` and capture the renderer diagnostics (`get_diagnostics()` plus any HRESULTs) in `docs/troubleshooting_ground_truth.md` before adjusting staging depth or toggling the WARP/debug environment variables. The RTX 5090 host still trips the allocation path when `staging_buffer_count=1`, so use a higher staging depth in the orchestrator until the allocator is fixed.

- **`just` is not recognized** – Install the utility via `winget install --id Casey.Just` or
  download the latest release from GitHub and add it to `PATH`. Alternatively, run the
  equivalent `python -m build`/`python -m pytest` commands directly.

