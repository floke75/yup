alias c := clean

gtest_filter := "*"

[doc("list available recipes")]
default:
  @just --list

[confirm("Are you sure you want to clean the build folder? [y/N]")]
[doc("clean project build artifacts")]
clean:
  rm -Rf build/*

[doc("build project using cmake")]
build CONFIG="Debug":
  cmake --build build --config {{CONFIG}}

[doc("execute unit tests using cmake")]
test CONFIG="Debug":
  cmake -G Xcode -B build
  cmake --build build --target yup_tests --config {{CONFIG}}
  build/tests/{{CONFIG}}/yup_tests.app/Contents/MacOS/yup_tests --gtest_filter={{gtest_filter}}

[confirm("Unsupported on this Windows-focused branch. Type 'mac' to continue anyway.")]
[doc("(Unsupported) generate and open project in macOS using Xcode")]
mac PROFILING="OFF":
  cmake -G Xcode -B build -DYUP_ENABLE_PROFILING={{PROFILING}}
  -open build/yup.xcodeproj

[doc("generate and open project using Ninja multi config")]
ninja PROFILING="OFF":
  cmake -G "Ninja Multi-Config" -B build -DYUP_ENABLE_PROFILING={{PROFILING}}

[doc("Windows: configure a Visual Studio 2022 solution for the Direct3D renderer and bindings, then open it in the IDE")]
win PROFILING="OFF":
  cmake -G "Visual Studio 17 2022" -B build -DYUP_ENABLE_PROFILING={{PROFILING}}
  -start build/yup.sln

[confirm("Unsupported on this Windows-focused branch. Type 'linux' to continue anyway.")]
[doc("(Unsupported) generate project in Linux using Ninja")]
linux PROFILING="OFF":
  @just ninja {{PROFILING}}

[confirm("Unsupported on this Windows-focused branch. Type 'ios' to continue anyway.")]
[doc("(Unsupported) generate and open project for iOS using Xcode")]
ios PLATFORM="OS64":
  cmake -G Xcode -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ios.cmake -DPLATFORM={{PLATFORM}}
  -open build/yup.xcodeproj

[confirm("Unsupported on this Windows-focused branch. Type 'ios-sim' to continue anyway.")]
[doc("(Unsupported) generate and open project for iOS Simulator macOS using Xcode")]
ios_simulator PLATFORM="SIMULATORARM64":
  @just ios {{PLATFORM}}

[confirm("Unsupported on this Windows-focused branch. Type 'android-mac' to continue anyway.")]
[doc("(Unsupported) generate and open project for Android using Android Studio (macos)")]
[macos]
android:
  cmake -G Xcode -B build -DYUP_TARGET_ANDROID=ON
  -open -a /Applications/Android\ Studio.app build/examples/render

[confirm("Unsupported on this Windows-focused branch. Type 'android-win' to continue anyway.")]
[doc("(Unsupported) generate and open project for Android using Android Studio (windows)")]
[windows]
android:
  cmake -G "Visual Studio 17 2022" -B build -DYUP_TARGET_ANDROID=ON

[confirm("Unsupported on this Windows-focused branch. Type 'android-linux' to continue anyway.")]
[doc("(Unsupported) generate and open project for Android using Android Studio (linux)")]
[linux]
android:
  cmake -G "Unix Makefiles" -B build -DYUP_TARGET_ANDROID=ON

[confirm("Unsupported on this Windows-focused branch. Type 'wasm' to continue anyway.")]
[doc("(Unsupported) generate and build project for WASM")]
emscripten CONFIG="Debug":
  emcc -v
  emcmake cmake -G "Ninja Multi-Config" -B build
  @just build {{CONFIG}}

[confirm("Unsupported on this Windows-focused branch. Type 'wasm-test' to continue anyway.")]
[doc("(Unsupported) run tests for WASM")]
emscripten_test CONFIG="Debug":
  @just build {{CONFIG}}
  node build/tests/{{CONFIG}}/yup_tests.js --gtest_filter={{gtest_filter}}

[confirm("Unsupported on this Windows-focused branch. Type 'wasm-serve' to continue anyway.")]
[doc("(Unsupported) serve project for WASM")]
emscripten_serve:
  python3 -m http.server -d .
  #python3 tools/serve.py -p 8000 -d .

[doc("Windows: build a release wheel, install it locally, and trigger pytest to validate the bindings before publishing")]
[working-directory: 'python']
python_wheel:
  python -m build --wheel
  @just python_install
  @just python_test

[doc("Windows: reinstall the freshly built wheel into the active venv for validation")]
[working-directory: 'python']
python_install:
  python -m pip install --force-reinstall dist/yup-*.whl

[doc("Windows: remove the wheel from the active venv when iterating on local builds")]
[working-directory: 'python']
python_uninstall:
  python -m pip uninstall -y yup

[working-directory: 'python']
python_test *TEST_OPTS:
  python -m pytest -s {{TEST_OPTS}}

[doc("Windows: quick smoke covering renderer bindings and NDI orchestrator entry points used during wheel validation")]
python_smoke:
  @just python_test tests/test_yup_rive_renderer/test_binding_interface.py -q
  @just python_test tests/test_yup_ndi/test_orchestrator.py -q
