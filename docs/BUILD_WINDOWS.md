# Windows Build Guide

This guide walks through building the YUP renderer, running its test suite, and producing the Python extension on a **fresh Windows 11 installation**. The steps below assume you are starting from an empty machine and want a repeatable process that any contributor can follow. Use it alongside `docs/Windows Build and Packaging.md`—that document expands on smoke tests, packaging, and troubleshooting.

## 1. Install Required Software

1. **Visual Studio 2022**
   - Download the [Visual Studio installer](https://visualstudio.microsoft.com/downloads/).
   - Install the *Desktop development with C++* workload.
   - Within the workload, ensure the following optional components are selected:
     - MSVC v143 build tools for x86 and x64
     - Windows 10 SDK (10.0.19041 or newer) or Windows 11 SDK
     - C++ CMake tools for Windows
     - C++ ATL for latest v143 build tools (optional but recommended)
2. **CMake 3.28 or newer**
   - Visual Studio bundles an older version. Install the latest release from [cmake.org/download](https://cmake.org/download/), and choose the option to add CMake to the system PATH for all users.
3. **Ninja 1.11 or newer**
   - Download the Windows zip from the [Ninja releases page](https://github.com/ninja-build/ninja/releases), extract `ninja.exe` somewhere on `PATH`, and verify with `ninja --version`.
4. **Python 3.11 (or newer 3.10+)**
   - Install from [python.org](https://www.python.org/downloads/windows/), making sure to check the option to *Add python.exe to PATH*.
   - After installation, verify the interpreter with `python --version`.
5. *(Optional but recommended)* **Git for Windows**
   - Install from [git-scm.com](https://git-scm.com/download/win) to access Git Bash and command-line tooling.
6. *(Optional)* **[`just`](https://github.com/casey/just) command runner**
   - Install via `winget install --id Casey.Just`, download the latest release archive, or run `cargo install just`. The helper recipes invoked later (`just python_wheel`, `just python_smoke`) require it.
7. *(Optional)* **`vcpkg` with `libjpeg-turbo:x64-windows`**
   - Only needed when decoding JPEG textures during renderer smoke tests.

## 2. Clone the Repository

Open a *x64 Native Tools Command Prompt for VS 2022* (search from the Start menu) to ensure MSVC and the Windows SDK are on the PATH. Clone the repository with submodules so that the vendored dependencies are available locally.

```bat
set ROOT=%USERPROFILE%\source\repos
mkdir "%ROOT%" 2>nul
cd "%ROOT%"
git clone --recurse-submodules https://github.com/kunitoki/yup.git
cd yup
```

If you already cloned the repository without submodules, run:

```bat
git submodule update --init --recursive
```

## 3. Configure the Build with CMake

Create a dedicated build directory to keep generated files separate from the source tree. The generator below targets Visual Studio 17 2022 and configures the entire project, including the renderer, tests, and Python module. Run this from the repository root (next to `CMakeLists.txt`).

```bat
cmake -S . -B build\msvc-release ^
  -G "Visual Studio 17 2022" ^
  -A x64 ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DYUP_BUILD_TESTS=ON ^
  -DYUP_BUILD_EXAMPLES=OFF
```

- `-S` and `-B` select the source and build folders.
- `-G` and `-A` ensure we target the 64-bit MSVC toolchain.
- `-DYUP_BUILD_TESTS=ON` enables GoogleTest targets.
- `-DYUP_BUILD_EXAMPLES=OFF` speeds up compilation when you only need the renderer pipeline.
- `-DYUP_ENABLE_AUDIO_MODULES=OFF` disables legacy subsystems that are turned off on this Windows-focused branch.

## 4. Build All Targets

Compile the solution in **Release** mode, including the renderer library, pybind11 module, and tests. Building Debug afterwards reuses the same generated solution, so you can re-run the command with `--config Debug` once Release succeeds.

```bat
cmake --build build\msvc-release --config Release
```

> [!NOTE]
> Modules that include C++ implementation files are now produced as static libraries during
> this step. Dependencies (Rive, HarfBuzz, libpng, etc.) are automatically linked through the
> module metadata, so consumers no longer have to repeat include directories or compile
> definitions when targeting `yup_gui` or the renderer bindings.

The command produces:
- `yup_rive_renderer.pyd` – the Python extension module.
- All static and dynamic libraries for the renderer core.
- Unit test executables located under `build\msvc-release\tests\Release`.

## 5. Run the Test Suite (Optional but Recommended)

From the same Developer Command Prompt, execute the compiled tests via CTest:

```bat
ctest --test-dir build\msvc-release --config Release
```

To exercise the Python tests (requires the `.pyd` extension on the Python path), either rely on the helper recipe or invoke the commands manually:

```bat
rem Option 1: helper recipe
just python_smoke

rem Option 2: manual invocation
set PYTHONPATH=%CD%\build\msvc-release\python\Release
python -m pytest python\tests -vv
```

## 6. Locate the Python Extension

After a successful build, the compiled module is located at:

```
build\msvc-release\python\Release\yup_rive_renderer.pyd
```

Copy this file into `python\` (or your virtual environment) when you want to experiment interactively.

## 7. Packaging Into a Wheel

To generate a distributable wheel, run the helper script once the Release build completes:

```bat
python tools\package_wheel.py
```

The script will:
1. Invoke CMake in Release mode if the project is not already built.
2. Copy `yup_rive_renderer.pyd` into the Python packaging directory.
3. Call `python -m build --wheel` inside `python\`, producing the wheel under `python\dist`.

You can trigger the same workflow via `just python_wheel` (from the repository root) when the `just` runner is installed.

You can now install the wheel with:

```bat
python -m pip install --force-reinstall python\dist\yup-*.whl
```

---

Following these steps gives any Windows developer a reliable, reproducible process for cloning, building, testing, and packaging the YUP renderer pipeline.
