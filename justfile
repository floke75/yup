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

[doc("generate and open project in macOS using Xcode")]
mac PROFILING="OFF":
  cmake -G Xcode -B build -DYUP_ENABLE_PROFILING={{PROFILING}}
  -open build/yup.xcodeproj

[doc("generate and open project using Ninja multi config")]
ninja PROFILING="OFF":
  cmake -G "Ninja Multi-Config" -B build -DYUP_ENABLE_PROFILING={{PROFILING}}

[doc("generate and open project in Windows using Visual Studio")]
win PROFILING="OFF":
  cmake -G "Visual Studio 17 2022" -B build -DYUP_ENABLE_PROFILING={{PROFILING}}
  -start build/yup.sln

[doc("generate project in Linux using Ninja")]
linux PROFILING="OFF":
  @just ninja {{PROFILING}}

[doc("generate and open project for iOS using Xcode")]
ios PLATFORM="OS64":
  cmake -G Xcode -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/ios.cmake -DPLATFORM={{PLATFORM}}
  -open build/yup.xcodeproj

[doc("generate and open project for iOS Simulator macOS using Xcode")]
ios_simulator PLATFORM="SIMULATORARM64":
  @just ios {{PLATFORM}}

[doc("generate and open project for Android using Android Studio (macos)")]
[macos]
android:
  cmake -G Xcode -B build -DYUP_TARGET_ANDROID=ON
  -open -a /Applications/Android\ Studio.app build/examples/render

[doc("generate and open project for Android using Android Studio (windows)")]
[windows]
android:
  cmake -G "Visual Studio 17 2022" -B build -DYUP_TARGET_ANDROID=ON

[doc("generate and open project for Android using Android Studio (linux)")]
[linux]
android:
  cmake -G "Unix Makefiles" -B build -DYUP_TARGET_ANDROID=ON

[doc("generate and build project for WASM")]
emscripten CONFIG="Debug":
  emcc -v
  emcmake cmake -G "Ninja Multi-Config" -B build
  @just build {{CONFIG}}

[doc("run tests for WASM")]
emscripten_test CONFIG="Debug":
  @just build {{CONFIG}}
  node build/tests/{{CONFIG}}/yup_tests.js --gtest_filter={{gtest_filter}}

[doc("serve project for WASM")]
emscripten_serve:
  python3 -m http.server -d .
  #python3 tools/serve.py -p 8000 -d .

[working-directory: 'python']
python_wheel:
  python -m build --wheel
  @just python_install
  @just python_test

[working-directory: 'python']
python_install:
  python -m pip install --force-reinstall dist/yup-*.whl

[working-directory: 'python']
python_uninstall:
  python -m pip uninstall -y yup

[working-directory: 'python']
python_test *TEST_OPTS:
  python -m pytest -s {{TEST_OPTS}}

[working-directory: 'python']
[doc("run renderer and NDI focused python smoke tests")]
python_test_rive_ndi:
  python -m pytest -q tests/test_yup_rive_renderer tests/test_yup_ndi
