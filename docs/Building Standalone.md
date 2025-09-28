# Building Standalone Applications with YUP

This guide explains how to create standalone applications using the YUP framework on Windows. It is scoped to the Windows-only Direct3D/NDI pipeline that the project actively maintains; any cross-platform remarks that remain are preserved strictly as legacy context.

### Legacy platforms

If you need historical notes for non-Windows targets, consult the repository history or earlier revisions of this document. New contributions should focus exclusively on the Windows toolchain and must not pursue feature parity on other platforms.

## Basic Application Structure

A basic YUP application consists of the following components:

1. A main application class that manages the application lifecycle
2. One or more window classes for the user interface
3. Application-specific resources and assets

Here's a minimal example of a standalone application:

```cpp
#include <yup_core/yup_core.h>
#include <yup_events/yup_events.h>
#include <yup_graphics/yup_graphics.h>
#include <yup_gui/yup_gui.h>

class MainWindow : public yup::DocumentWindow
{
public:
    MainWindow()
        : DocumentWindow (yup::ComponentNative::Options(), {})
    {
        setTitle ("My Application");
        setSize (800, 600);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
        takeFocus();
    }

    void paint (yup::Graphics& g) override
    {
        g.fillAll (yup::Colours::black);

        //g.setColour (yup::Colours::white);
        //g.setFont (16.0f);
        //g.drawText ("Hello, YUP!", getLocalBounds(), yup::Justification::centred);
    }

    void userTriedToCloseWindow() override
    {
        yup::YUPApplication::getInstance()->systemRequestedQuit();
    }

private:
    YUP_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class MyApplication : public yup::YUPApplication
{
public:
    MyApplication() = default;

    const yup::String getApplicationName() override
    {
        return "My Application";
    }

    const yup::String getApplicationVersion() override
    {
        return "1.0.0";
    }

    void initialise (const yup::String& commandLineParameters) override
    {
        window = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        window.reset();
    }

private:
    std::unique_ptr<MainWindow> window;
};

START_YUP_APPLICATION (MyApplication)
```

## Building with CMake (Windows)

Create a `CMakeLists.txt` file for your application:

```cmake
cmake_minimum_required (VERSION 3.28)

set (target_name my_app)
set (target_version "1.0.0")
project (${target_name} VERSION ${target_version})

include (FetchContent)

FetchContent_Declare(
  yup
  GIT_REPOSITORY https://github.com/kunitoki/yup.git
  GIT_TAG        main)

set (YUP_BUILD_EXAMPLES OFF)
set (YUP_BUILD_TESTS OFF)
FetchContent_MakeAvailable(yup)

# Create the standalone application target
yup_standalone_app (
    TARGET_NAME ${target_name}
    TARGET_VERSION ${target_version}
    TARGET_IDE_GROUP "MyApp"
    TARGET_APP_ID "com.mycompany.${target_name}"
    TARGET_APP_NAMESPACE "com.mycompany"
    TARGET_CXX_STANDARD 17
    INITIAL_MEMORY 268435456  # 256MB initial memory
    MODULES
        yup_audio_devices
        yup_gui)

# Add source files
file (GLOB sources "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
source_group (TREE ${CMAKE_CURRENT_LIST_DIR}/ FILES ${sources})
target_sources (${target_name} PRIVATE ${sources})

# Add resources if needed
# The Android guard below is legacy scaffolding retained for historical reference.
# Windows builds will include resources as shown here.
if (NOT YUP_TARGET_ANDROID)
    file (GLOB resources "${CMAKE_CURRENT_LIST_DIR}/resources/*")
    source_group (TREE ${CMAKE_CURRENT_LIST_DIR}/resources/ FILES ${resources})
    target_sources (${target_name} PRIVATE ${resources})
endif()
```

> **Note:** The raster image back-ends (`libpng`, `libwebp`, and `libjpeg` when available) are now pulled in automatically via
> the `rive_decoders` module that `yup_graphics` depends on. You only need to add those modules explicitly if your application
> consumes them outside of the graphics stack.
>
> On Windows the recommended path is to install the `libjpeg-turbo` SDK (for example via vcpkg) before configuring so JPEG raster support remains enabled. References to non-Windows package managers in older revisions should be treated as legacy guidance.

## Application Resources

### Resource Management

YUP provides several ways to manage application resources:

1. **Embedded Resources**

```cmake
// In your CMakeLists.txt
yup_add_embedded_binary_resources (
    "${target_name}_binary_data"
    OUT_DIR BinaryData
    HEADER BinaryData.h
    NAMESPACE MyApp
    RESOURCE_NAMES
        Settings
        Logo
    RESOURCES
        "resources/config/settings.json"
        "resources/images/logo.png")

yup_standalone_app (
    # ...
    MODULES
        yup_audio_devices
        yup_gui
        ${target_name}_binary_data) # << add this
```

```cpp
// In your code you can access these binaries
#include <BinaryData.h>

yup::MemoryInputStream is (MyApp::Settings_data, MyApp::Settings_size, false);
```

2. **File System Resources**
```cpp
// Get the application's data directory
auto dataDir = yup::File::getSpecialLocation (yup::File::userApplicationDataDirectory)
    .getChildFile (getApplicationName());

// Create if it doesn't exist
if (! dataDir.exists())
    dataDir.createDirectory();
```

## Best Practices

1. **Window Management**
   - Use `DocumentWindow` for main windows
   - Implement proper window closing behavior
   - Handle window resizing and positioning

2. **Resource Management**
   - Use RAII for resource allocation
   - Cache frequently used resources
   - Clean up resources in destructors

3. **UI Design**
   - Follow Windows UI guidance for window chrome, accessibility, and input behaviour
   - Implement responsive layouts that respect Windows scaling factors
   - Handle different DPI settings across high-DPI Windows displays

4. **Performance**
   - Minimize allocations in UI thread
   - Use background threads for heavy operations
   - Profile your application

5. **Testing**
   - Test on supported Windows 11 configurations
   - Verify window management (min/max/restore) with Direct3D surfaces
   - Check resource cleanup
   - Test different Windows display scale factors and multi-monitor layouts

## Common Issues and Solutions

1. **Window Issues**
   - Handle window focus properly
   - Manage window state (minimized, maximized)
   - Handle multiple monitors

2. **Resource Problems**
   - Check file permissions
   - Verify resource paths
   - Handle missing resources gracefully

3. **Windows-Specific Issues**
   - Integrate with the Windows menu system and shell behaviour
   - Manage window decorations across Win32 and UWP packaging targets if applicable
   - Use Windows file system conventions (wide-character paths, known folders APIs)

## Additional Resources

- [YUP Documentation](https://yup.github.io/docs)
- [YUP Examples](https://github.com/kunitoki/yup/tree/main/examples)
- [Platform-Specific Guidelines](https://yup.github.io/docs/platforms) *(legacy cross-platform reference; consult only if you are researching historical behaviour)*
