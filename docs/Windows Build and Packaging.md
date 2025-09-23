# Windows Build and Packaging Workflow

This guide captures the end-to-end workflow for building the Direct3D11 Rive renderer,
packaging the Python bindings, and validating the NDI orchestration path on Windows 11.
The steps assume Visual Studio 2022, Python 3.11+, and the Windows 10 or 11 SDK are
installed.

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

1. Configure the project with Visual Studio 2022 generators. Ninja Multi-Config also works,
   but Visual Studio simplifies debugging and symbol inspection.

   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DYUP_ENABLE_PROFILING=OFF
   ```

2. Build the default Debug configuration:

   ```powershell
   cmake --build build --config Debug --target ALL_BUILD
   ```

3. (Optional) Build the RelWithDebInfo configuration when preparing redistributable artefacts:

   ```powershell
   cmake --build build --config RelWithDebInfo --target ALL_BUILD
   ```

## 3. Build and install the Python wheel

1. From the repository root, build the wheel and install it into the active virtual
environment:

   ```powershell
   just python_wheel
   ```

   The recipe runs `python -m build --wheel`, reinstalls the freshly built package, and
   executes the Python unit tests.

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
immediately. The tests skip automatically when the native renderer or NDI dependencies
are unavailable.

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
