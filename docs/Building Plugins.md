# Building Plugins with YUP

This guide explains how to create audio plugins using the YUP framework. YUP supports multiple plugin formats including CLAP, VST3, and more.

## Basic Plugin Structure

A basic YUP plugin consists of the following components:

1. A processor class that handles audio processing
2. An editor class for the plugin's user interface
3. Plugin format-specific wrapper code

Here's a minimal example of a plugin:

```cpp
#include <yup_audio_plugin_client/yup_audio_plugin_client.h>

class MyPluginProcessor : public yup::AudioProcessor
{
public:
    MyPluginProcessor()
    {
        // Add parameters
        addParameter (gainParameter = yup::AudioParameterBuilder{}
            .withID ("gain")
            .withName ("Gain")
            .withRange (0.0f, 1.0f)
            .withDefault (0.5f)
            .build());
    }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override
    {
        // Initialize processing resources

        gain = yup::AudioParameterHandle (*gainParameter, sampleRate);
    }

    void releaseResources() override
    {
        // Clean up resources
    }

    void processBlock (yup::AudioBuffer<float>& audioBuffer, yup::MidiBuffer& midiMessages) override
    {
        if (gain.updateNextAudioBlock())
        {
            float currentGain = gain.getCurrentValue();
            float destinationGain = gain.skip (audioBuffer.getNumSamples());

            audioBuffer.applyGainRamp (0, audioBuffer.getNumSamples(), currentGain, destinationGain);
        }
        else
        {
            audioBuffer.applyGain (gain.getCurrentValue());
        }
    }

    bool hasEditor() const override { return true; }
    yup::AudioProcessorEditor* createEditor() override;

private:
    yup::AudioParameter::Ptr gainParameter;
    yup::AudioParameterHandle gain;
};

class MyPluginEditor : public yup::AudioProcessorEditor
{
public:
    MyPluginEditor (MyPluginProcessor& p)
        : AudioProcessorEditor (&p)
        , processor (p)
    {
        setSize (400, 300);
    }

    void paint (yup::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (yup::ResizableWindow::backgroundColourId));
    }

private:
    MyPluginProcessor& processor;
};

yup::AudioProcessorEditor* MyPluginProcessor::createEditor() override
{
    return new MyPluginEditor (*this);
}

// Plugin entry point
extern "C" yup::AudioProcessor* createPluginProcessor()
{
    return new MyPluginProcessor();
}
```

## Building with CMake

Create a `CMakeLists.txt` file for your plugin:

```cmake
cmake_minimum_required (VERSION 3.28)

set (target_name my_plugin)
set (target_version "0.0.1")
project (${target_name} VERSION ${target_version})

include (FetchContent)

FetchContent_Declare(
  yup
  GIT_REPOSITORY https://github.com/kunitoki/yup.git
  GIT_TAG        main)

set (YUP_BUILD_EXAMPLES OFF)
set (YUP_BUILD_TESTS OFF)
FetchContent_MakeAvailable(yup)

# Create the plugin target
yup_audio_plugin (
    TARGET_NAME ${target_name}
    TARGET_VERSION ${target_version}
    TARGET_IDE_GROUP "MyPlugin"
    TARGET_APP_ID "com.mycompany.${target_name}"
    TARGET_APP_NAMESPACE "com.mycompany"
    TARGET_CXX_STANDARD 17
    PLUGIN_ID "com.mycompany.MyPlugin"
    PLUGIN_NAME "MyPlugin"
    PLUGIN_VENDOR "com.mycompany"
    PLUGIN_EMAIL "support@mycompany.com"
    PLUGIN_VERSION "${target_version}"
    PLUGIN_DESCRIPTION "The best audio plugin ever."
    PLUGIN_URL "https://www.mycompany.com"
    PLUGIN_IS_SYNTH OFF
    PLUGIN_IS_MONO OFF
    PLUGIN_CREATE_CLAP ON
    PLUGIN_CREATE_VST3 ON
    PLUGIN_CREATE_STANDALONE ON
    MODULES
        yup::yup_gui
        yup::yup_audio_processors)

# Add source files
file (GLOB sources "${CMAKE_CURRENT_LIST_DIR}/*.cpp")
source_group (TREE ${CMAKE_CURRENT_LIST_DIR}/ FILES ${sources})
target_sources (${target_name}_shared PUBLIC ${sources})
```

## Building on Windows

Windows 11 with Visual Studio 2022 is the supported environment for producing YUP-based plugins. The steps below assume you are running from an **x64 Native Tools Command Prompt for VS 2022** with CMake 3.28 or newer installed.

### Prerequisites

- Visual Studio 2022 with the Desktop development with C++ workload and the latest Windows 10/11 SDK.
- CMake 3.28+ and Ninja (optional, but recommended for faster incremental builds).
- Git access to clone your plugin repository and YUP as a submodule or FetchContent dependency.

### Configure and Build

```powershell
# From the root of your plugin project
cmake -G "Visual Studio 17 2022" -A x64 `
  -B build `
  -DYUP_ENABLE_TESTS=OFF `
  -DYUP_ENABLE_EXAMPLES=OFF

cmake --build build --config Release
```

> 💡 Tip: The repository ships a helper script at `tools/install_windows.ps1` that can provision dependencies and run the same CMake generation flow. Use it when bootstrapping a fresh workstation.

### Packaging Artifacts

Visual Studio produces the plugin binaries inside `build/Release`. Copy the resulting `.vst3` or `.clap` bundles into the directories listed below, or adapt the install step of your CI pipeline to publish them.

## Plugin Installation on Windows

- **VST3**: `C:\\Program Files\\Common Files\\VST3\\`
- **CLAP**: `C:\\Program Files\\Common Files\\CLAP\\`

Ensure the destination folders already exist and that you are running an elevated shell when writing to `%ProgramFiles%`. Some hosts also scan `%COMMONPROGRAMFILES%\VST3` automatically—verify your DAW's plugin search paths if the build does not appear.

> ⚠️ **Legacy reference only:** macOS and Linux installation paths from older branches remain `~/Library/Audio/Plug-Ins/(VST3|CLAP)/` and `~/.vst3` / `~/.clap` respectively. These flows are not supported or tested on the current Windows-focused branch.

## Legacy Appendix: Non-Windows Build Commands (Unsupported)

The commands below are retained for historical context. They are **not** validated in this branch and should be treated as references when porting the project back to macOS or Linux.

### macOS (legacy)
```bash
cmake -G "Xcode" . -B build -DYUP_ENABLE_TESTS=OFF -DYUP_ENABLE_EXAMPLES=OFF
cmake --build build --config Release
```

### Linux (legacy)
```bash
cmake -G "Ninja" . -B build -DYUP_ENABLE_TESTS=OFF -DYUP_ENABLE_EXAMPLES=OFF
cmake --build build --config Release
```

## Best Practices

1. **Parameter Management**
   - Use `addParameter()` in the processor constructor
   - Handle parameter changes in `processBlock()`

2. **Editor Design**
   - Keep the editor lightweight
   - Handle resizing appropriately

3. **Resource Management**
   - Initialize resources in `prepareToPlay()`
   - Clean up in `releaseResources()`
   - Use RAII for resource management

4. **Performance**
   - Minimize allocations in the audio thread
   - Use SIMD operations when possible
   - Profile your plugin for CPU usage

5. **Testing**
   - Test your plugin in different DAWs
   - Verify parameter automation
   - Check for memory leaks
   - Test different sample rates and buffer sizes

## Common Issues and Solutions

1. **Plugin Not Showing Up**
   - Verify installation path
   - Check plugin format compatibility
   - Ensure proper permissions

2. **Audio Issues**
   - Verify buffer sizes
   - Check for denormal numbers
   - Monitor CPU usage

3. **UI Problems**
   - Handle window resizing
   - Manage component lifetimes
   - Test different DPI settings

## Additional Resources

- [YUP Documentation](https://yup.github.io/docs)
- [CLAP Documentation](https://cleveraudio.org/)
- [VST3 Documentation](https://developer.steinberg.help/hub/display/VST)
