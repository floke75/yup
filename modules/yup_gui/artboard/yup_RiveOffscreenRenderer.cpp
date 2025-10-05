/*
  ==============================================================================

   This file is part of the YUP library.
   Copyright (c) 2025 - kunitoki@gmail.com

   YUP is an open source library subject to open-source licensing.

   The code included in this file is provided under the terms of the ISC license
   http://www.isc.org/downloads/software-support-policy/isc-license. Permission
   to use, copy, modify, and/or distribute this software for any purpose with or
   without fee is hereby granted provided that the above copyright notice and
   this permission notice appear in all copies.

   YUP IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

#include "yup_gui/yup_gui.h"

#include "yup_RiveOffscreenRenderer.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#if YUP_WINDOWS && YUP_RIVE_USE_D3D

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "rive/layout.hpp"
#include "rive/animation/linear_animation_instance.hpp"
#include "rive/animation/state_machine_instance.hpp"
#include "rive/static_scene.hpp"
#include "rive/renderer/d3d/d3d.hpp"
#include "rive/renderer/d3d11/render_context_d3d_impl.hpp"
#include "rive/renderer/rive_renderer.hpp"

namespace yup
{

namespace
{
constexpr wchar_t kPresentationWindowClassName[] = L"YupRivePreviewWindow";
constexpr wchar_t kPresentationWindowTitle[] = L"YUP Rive Preview";
constexpr DXGI_FORMAT kRenderFormat = DXGI_FORMAT_B8G8R8A8_UNORM;

[[nodiscard]] std::string makeErrorMessage (HRESULT hr)
{
    std::array<wchar_t, 256> buffer {};
    ::FormatMessageW (FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                      nullptr,
                      static_cast<DWORD> (hr),
                      0,
                      buffer.data(),
                      static_cast<DWORD> (buffer.size()),
                      nullptr);

    return String (buffer.data()).trim().toStdString();
}

[[nodiscard]] D3D11_TEXTURE2D_DESC makeTextureDescription (UINT width, UINT height, D3D11_USAGE usage, UINT bindFlags, UINT cpuFlags)
{
    D3D11_TEXTURE2D_DESC desc {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = kRenderFormat;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = usage;
    desc.BindFlags = bindFlags;
    desc.CPUAccessFlags = cpuFlags;
    desc.MiscFlags = 0;
    return desc;
}

[[nodiscard]] rive::gpu::RenderContext::FrameDescriptor makeFrameDescriptor (int width, int height)
{
    rive::gpu::RenderContext::FrameDescriptor descriptor {};
    descriptor.renderTargetWidth = static_cast<uint32_t> (width);
    descriptor.renderTargetHeight = static_cast<uint32_t> (height);
    descriptor.loadAction = rive::gpu::LoadAction::clear;
    descriptor.clearColor = 0x00000000;
    return descriptor;
}

} // namespace

struct RiveOffscreenRenderer::Impl
{
    /*
        Implementation detail: this struct intentionally keeps a narrow surface
        area so the upcoming refactor can collapse any unused YUP facilities
        without breaking the Direct3D pipeline. When touching the members below,
        double-check the Python bindings (`python/src/yup_rive_renderer.cpp`) and
        the renderer tests (`tests/yup_gui/yup_RiveOffscreenRenderer.cpp`) because
        they exercise the same behaviour. Delete helper methods only when the
        tests prove the orchestrator still receives deterministic frames.
    */
    enum class FrameState
    {
        Available,
        Writing,
        PendingRead,
        Reading
    };

    explicit Impl (int widthIn, int heightIn, std::size_t stagingBufferCountIn, bool enablePresentation)
        : width (std::max (widthIn, 0)),
          height (std::max (heightIn, 0)),
          rowStride (static_cast<std::size_t> (std::max (widthIn, 0)) * 4u),
          frameSize (rowStride * static_cast<std::size_t> (std::max (heightIn, 0))),
          stagingBufferCount (std::max<std::size_t> (std::size_t { 1 }, stagingBufferCountIn)),
          presentationRequested (enablePresentation)
    {
        if (widthIn <= 0 || heightIn <= 0)
        {
            lastError = String::formatted (
                "Renderer dimensions must be positive (received %dx%d)",
                widthIn,
                heightIn);
            appendDiagnostic ("error", lastError);
            return;
        }

        try
        {
            stagingTextures.resize (stagingBufferCount);
            stagingBuffers.assign (stagingBufferCount, std::vector<uint8> (frameSize, 0));
            frameStates.assign (stagingBufferCount, FrameState::Available);
            frameSnapshot = std::make_shared<std::vector<uint8>> (frameSize, 0);
        }
        catch (const std::bad_alloc& alloc)
        {
            lastError = String::formatted ("Failed to allocate staging buffers (%s)", alloc.what());
            appendDiagnostic ("error", lastError);
            return;
        }
        catch (...)
        {
            lastError = "Failed to allocate staging buffers due to an unknown error";
            appendDiagnostic ("error", lastError);
            return;
        }

        initialise();
    }

    ~Impl() = default;

    static String describeMapFailure (HRESULT hr);

    bool isValid() const noexcept { return initialised; }

    void setPresentationEnabled (bool shouldEnable)
    {
        presentationRequested = shouldEnable;

        if (! initialised)
            return;

        if (shouldEnable)
        {
            if (! presentationEnabled)
                initialisePresentationResources();
        }
        else if (presentationEnabled)
        {
            releasePresentationResources();
        }
    }

    bool isPresentationEnabled() const noexcept { return presentationEnabled; }

    Result load (const File& fileToLoad, const String& artboardName)
    {
        logInfo (String::formatted ("Loading Rive file '%s' (%lld bytes)",
                                    fileToLoad.getFullPathName().toRawUTF8(),
                                    static_cast<long long> (fileToLoad.getSize())));
        return loadInternal (
            [&fileToLoad] (rive::Factory& factory) { return ArtboardFile::load (fileToLoad, factory); }, artboardName);
    }

    Result load (Span<const uint8> bytes, const String& artboardName)
    {
        logInfo (String::formatted ("Loading Rive data from memory (%zu bytes)", bytes.size()));
        return loadInternal (
            [&bytes] (rive::Factory& factory)
            {
                MemoryInputStream stream (bytes.data(), bytes.size(), false);
                return ArtboardFile::load (stream, factory);
            },
            artboardName);
    }

    Result loadInternal (const std::function<ArtboardFile::LoadResult (rive::Factory&)>& loader,
                         const String& artboardName)
    {
        clearDiagnostics();
        lastError.clear();

        const auto failWith = [this] (String message)
        {
            lastError = std::move (message);
            return Result::fail (lastError);
        };

        if (! initialised)
            return failWith ("Rive offscreen renderer is not available");

        if (renderContext == nullptr)
            return failWith ("Missing Rive render context");

        auto loadResult = loader (*renderContext);
        if (! loadResult)
        {
            lastError = loadResult.getErrorMessage();
            logWarning (String ("Failed to load Rive file: ") + lastError);
            appendDiagnostic ("error", lastError);
            return Result::fail (lastError);
        }

        artboardFile = loadResult.getValue();

        if (auto* riveFile = artboardFile->getRiveFile())
        {
            logInfo (String::formatted ("Loaded Rive file with %u artboard(s)",
                                        static_cast<unsigned int> (riveFile->artboardCount())));
        }

        return selectArtboardInternal (artboardName);
    }

    Result selectArtboard (const String& artboardName)
    {
        lastError.clear();

        return selectArtboardInternal (artboardName);
    }

    Result selectArtboardInternal (const String& artboardName)
    {
        const auto failWith = [this] (String message)
        {
            lastError = std::move (message);
            return Result::fail (lastError);
        };

        if (! initialised)
            return failWith ("Rive offscreen renderer is not available");

        if (artboardFile == nullptr)
            return failWith ("No Rive file has been loaded");

        auto* riveFile = artboardFile->getRiveFile();
        if (riveFile == nullptr)
            return failWith ("Loaded Rive file is invalid");

        std::unique_ptr<rive::ArtboardInstance> loadedArtboard;

        if (artboardName.isNotEmpty())
            loadedArtboard = riveFile->artboardNamed (artboardName.toStdString());
        else
            loadedArtboard = riveFile->artboardDefault();

        if (loadedArtboard == nullptr)
        {
            if (artboardName.isNotEmpty())
                return failWith ("Unable to find artboard named '" + artboardName + "'");

            return failWith ("Rive file does not contain a default artboard");
        }

        return setActiveArtboard (std::move (loadedArtboard));
    }

    StringArray listArtboards() const
    {
        StringArray names;

        if (artboardFile == nullptr)
            return names;

        if (auto* riveFile = artboardFile->getRiveFile())
        {
            const auto artboardCount = riveFile->artboardCount();

            for (std::size_t index = 0; index < artboardCount; ++index)
                names.add (String (riveFile->artboardNameAt (index)));
        }

        return names;
    }

    StringArray listAnimations() const
    {
        StringArray names;

        if (artboard == nullptr)
            return names;

        const auto animationCount = artboard->animationCount();
        for (std::size_t index = 0; index < animationCount; ++index)
        {
            if (auto* animation = artboard->animation (index))
                names.add (String (animation->name()));
        }

        return names;
    }

    StringArray listStateMachines() const
    {
        StringArray names;

        if (artboard == nullptr)
            return names;

        const auto machineCount = artboard->stateMachineCount();
        for (std::size_t index = 0; index < machineCount; ++index)
        {
            if (auto* machine = artboard->stateMachine (index))
                names.add (String (machine->name()));
        }

        return names;
    }

    bool playAnimation (const String& name, bool loop)
    {
        if (artboard == nullptr)
            return false;

        animation.reset();
        stateMachine.reset();
        sceneHolder.reset();

        animation = artboard->animationNamed (name.toStdString());
        if (animation == nullptr)
            return false;

        animation->loopValue (loop ? static_cast<int> (rive::Loop::loop) : static_cast<int> (rive::Loop::oneShot));
        scene = animation.get();
        scene->advanceAndApply (0.0f);
        paused = false;
        return renderFrame();
    }

    bool playStateMachine (const String& name)
    {
        if (artboard == nullptr)
            return false;

        animation.reset();
        stateMachine.reset();
        sceneHolder.reset();

        stateMachine = artboard->stateMachineNamed (name.toStdString());
        if (stateMachine == nullptr)
            return false;

        scene = stateMachine.get();
        scene->advanceAndApply (0.0f);
        paused = false;
        return renderFrame();
    }

    void stop()
    {
        animation.reset();
        stateMachine.reset();
        sceneHolder.reset();
        scene = nullptr;
        paused = false;
    }

    bool setBoolInput (const String& name, bool value)
    {
        if (stateMachine == nullptr)
            return false;

        if (auto* input = stateMachine->getBool (name.toStdString()))
        {
            input->value (value);
            return true;
        }

        return false;
    }

    bool setNumberInput (const String& name, double value)
    {
        if (stateMachine == nullptr)
            return false;

        if (auto* input = stateMachine->getNumber (name.toStdString()))
        {
            input->value (static_cast<float> (value));
            return true;
        }

        return false;
    }

    bool fireTrigger (const String& name)
    {
        if (stateMachine == nullptr)
            return false;

        if (auto* trigger = stateMachine->getTrigger (name.toStdString()))
        {
            trigger->fire();
            return true;
        }

        return false;
    }

    bool advance (float deltaSeconds)
    {
        pumpWindowMessages();

        if (! initialised || paused || scene == nullptr)
            return false;

        const auto keepAnimating = scene->advanceAndApply (deltaSeconds);
        const auto rendered = renderFrame();
        return keepAnimating && rendered;
    }

    void setPaused (bool shouldPause) { paused = shouldPause; }
    bool isPaused() const noexcept { return paused; }

    int getWidth() const noexcept { return width; }
    int getHeight() const noexcept { return height; }
    std::size_t getStride() const noexcept { return rowStride; }

    const std::vector<uint8>& getFrameBuffer() const noexcept { return *ensureFrameSnapshot(); }
    std::shared_ptr<const std::vector<uint8>> getFrameBufferShared() const noexcept
    {
        return ensureFrameSnapshot();
    }

    const String& getLastError() const noexcept { return lastError; }
    String getDiagnosticsReport() const
    {
        std::scoped_lock lock (diagnosticsMutex);
        return diagnosticsLog;
    }

    String getActiveArtboardName() const { return activeArtboardName; }

private:

    struct PresentationResources
    {
        Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView;
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        HWND hwnd = nullptr;
    };

    void clearDiagnostics() const
    {
        std::scoped_lock lock (diagnosticsMutex);
        diagnosticsLog = {};
    }

    void appendDiagnostic (const char* level, const String& message) const
    {
        std::scoped_lock lock (diagnosticsMutex);

        if (diagnosticsLog.isNotEmpty())
            diagnosticsLog += '\n';

        diagnosticsLog += String::formatted ("[%s] %s", level, message.toRawUTF8());
    }

    struct AdapterInfo
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        DXGI_ADAPTER_DESC1 desc {};
    };

    void enumerateAdapters()
    {
        availableAdapters.clear();

        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        auto hr = ::CreateDXGIFactory1 (__uuidof (IDXGIFactory1), reinterpret_cast<void**> (factory.GetAddressOf()));
        if (FAILED (hr))
        {
            const auto message = String::formatted (
                "CreateDXGIFactory1 failed (0x%08X): %s",
                static_cast<unsigned int> (hr),
                makeErrorMessage (hr).c_str());
            logWarning (message);
            appendDiagnostic ("error", message);
            return;
        }

        logInfo ("Enumerating DXGI adapters");
        appendDiagnostic ("info", "Enumerating DXGI adapters");

        UINT index = 0;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

        while (factory->EnumAdapters1 (index, adapter.ReleaseAndGetAddressOf()) != DXGI_ERROR_NOT_FOUND)
        {
            DXGI_ADAPTER_DESC1 desc {};
            if (SUCCEEDED (adapter->GetDesc1 (&desc)))
            {
                const auto dedicatedGiB = static_cast<double> (desc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0);
                const auto message = String::formatted ("Adapter %u: %s (vendor=0x%04X, device=0x%04X, VRAM=%.2f GiB, flags=0x%08X)",
                                                    static_cast<unsigned int> (index),
                                                    String (desc.Description).toRawUTF8(),
                                                    static_cast<unsigned int> (desc.VendorId),
                                                    static_cast<unsigned int> (desc.DeviceId),
                                                    dedicatedGiB,
                                                    static_cast<unsigned int> (desc.Flags));
                logInfo (message);
                appendDiagnostic ("info", message);

                AdapterInfo info;
                info.adapter = adapter;
                info.desc = desc;
                availableAdapters.push_back (std::move (info));
            }
            else
            {
                const auto warning = String::formatted ("Adapter %u: failed to query description", static_cast<unsigned int> (index));
                logWarning (warning);
                appendDiagnostic ("warning", warning);
            }

            ++index;
        }

        if (index == 0)
        {
            const auto warning = "No DXGI adapters were reported by the runtime";
            logWarning (warning);
            appendDiagnostic ("warning", warning);
        }
    }

    void initialise()
    {
        using Microsoft::WRL::ComPtr;

        clearDiagnostics();
        lastError.clear();

        const auto isTruthy = [] (const char* value)
        {
            if (value == nullptr)
                return false;

            String str (value);
            str = str.trim();

            if (str.isEmpty())
                return false;

            return str.equalsIgnoreCase ("1")
                || str.equalsIgnoreCase ("true")
                || str.equalsIgnoreCase ("yes")
                || str.equalsIgnoreCase ("on")
                || str.equalsIgnoreCase ("warp");
        };

        const bool forceWarp = isTruthy (std::getenv ("YUP_RIVE_FORCE_WARP"));
        const bool requestDebugLayer = isTruthy (std::getenv ("YUP_RIVE_ENABLE_D3D_DEBUG"));

        const auto initialMessage = String::formatted ("Initialising Direct3D11 offscreen renderer (%dx%d, staging=%zu, presentation=%s, frame=%zu bytes)",
                                    width,
                                    height,
                                    stagingBufferCount,
                                    presentationRequested ? "enabled" : "disabled",
                                    frameSize);
        logInfo (initialMessage);
        appendDiagnostic ("info", initialMessage);
        appendDiagnostic ("info", "Direct3D backend: RHI=DX11 (YUP_RIVE_USE_D3D=1)");

        if (forceWarp)
        {
            const auto warpMessage = String ("YUP_RIVE_FORCE_WARP set - forcing WARP device creation");
            logInfo (warpMessage);
            appendDiagnostic ("info", warpMessage);
        }

        if (requestDebugLayer)
        {
            const auto debugMessage = String ("YUP_RIVE_ENABLE_D3D_DEBUG set - requesting D3D11 debug layer");
            logInfo (debugMessage);
            appendDiagnostic ("info", debugMessage);
        }

        enumerateAdapters();

        const D3D_FEATURE_LEVEL requestedLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

        const UINT baseCreationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        const UINT debugCreationFlags = baseCreationFlags | D3D11_CREATE_DEVICE_DEBUG;
        std::vector<UINT> creationFlagAttempts;
#if YUP_DEBUG
        creationFlagAttempts.push_back (debugCreationFlags);
#endif
        if (requestDebugLayer && std::find (creationFlagAttempts.begin(), creationFlagAttempts.end(), debugCreationFlags) == creationFlagAttempts.end())
            creationFlagAttempts.insert (creationFlagAttempts.begin(), debugCreationFlags);

        if (std::find (creationFlagAttempts.begin(), creationFlagAttempts.end(), baseCreationFlags) == creationFlagAttempts.end())
            creationFlagAttempts.push_back (baseCreationFlags);

        ComPtr<ID3D11Device> createdDevice;
        ComPtr<ID3D11DeviceContext> createdContext;
        D3D_FEATURE_LEVEL createdFeatureLevel = D3D_FEATURE_LEVEL_11_0;

        const auto describeFailure = [] (const char* driverName, UINT flags, HRESULT hr)
        {
            const auto message = makeErrorMessage (hr);
            const bool debugEnabled = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;

            if (! message.empty())
            {
                return String::formatted (
                    "D3D11CreateDevice (%s, flags=0x%08X%s) failed (0x%08X): %s",
                    driverName,
                    static_cast<unsigned int> (flags),
                    debugEnabled ? ", debug" : "",
                    static_cast<unsigned int> (hr),
                    message.c_str());
            }

            return String::formatted (
                "D3D11CreateDevice (%s, flags=0x%08X%s) failed (0x%08X)",
                driverName,
                static_cast<unsigned int> (flags),
                debugEnabled ? ", debug" : "",
                static_cast<unsigned int> (hr));
        };

        const auto attemptDeviceCreation = [&] (IDXGIAdapter1* adapter,
                                                D3D_DRIVER_TYPE driverType,
                                                UINT creationFlags,
                                                ComPtr<ID3D11Device>& deviceOut,
                                                ComPtr<ID3D11DeviceContext>& contextOut,
                                                D3D_FEATURE_LEVEL& featureLevelOut) -> HRESULT
        {
            ComPtr<ID3D11Device> tempDevice;
            ComPtr<ID3D11DeviceContext> tempContext;
            D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

            auto hr = D3D11CreateDevice (adapter,
                                         driverType,
                                         nullptr,
                                         creationFlags,
                                         requestedLevels,
                                         static_cast<UINT> (std::size (requestedLevels)),
                                         D3D11_SDK_VERSION,
                                         tempDevice.GetAddressOf(),
                                         &featureLevel,
                                         tempContext.GetAddressOf());

            if (FAILED (hr) && hr == E_INVALIDARG)
            {
                hr = D3D11CreateDevice (adapter,
                                         driverType,
                                         nullptr,
                                         creationFlags,
                                         nullptr,
                                         0,
                                         D3D11_SDK_VERSION,
                                         tempDevice.GetAddressOf(),
                                         &featureLevel,
                                         tempContext.GetAddressOf());
            }

            if (SUCCEEDED (hr))
            {
                deviceOut = std::move (tempDevice);
                contextOut = std::move (tempContext);
                featureLevelOut = featureLevel;
            }

            return hr;
        };

        const auto createDeviceCommon = [&] (IDXGIAdapter1* adapter,
                                             D3D_DRIVER_TYPE driverType,
                                             const char* driverName,
                                             String& errorOut,
                                             const String* adapterDescription) -> bool
        {
            String combinedErrors;

            for (auto flags : creationFlagAttempts)
            {
                const auto debugSuffix = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0 ? ", debug" : "";
                const auto attemptMessage = adapterDescription != nullptr
                    ? String::formatted ("Attempting D3D11CreateDevice (%s, flags=0x%08X%s)",
                                         adapterDescription->toRawUTF8 (),
                                         static_cast<unsigned int> (flags),
                                         debugSuffix)
                    : String::formatted ("Attempting D3D11CreateDevice (%s, flags=0x%08X%s)",
                                         driverName,
                                         static_cast<unsigned int> (flags),
                                         debugSuffix);
                logInfo (attemptMessage);
                appendDiagnostic ("info", attemptMessage);

                ComPtr<ID3D11Device> tempDevice;
                ComPtr<ID3D11DeviceContext> tempContext;
                D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

                const auto hr = attemptDeviceCreation (adapter,
                                                        driverType,
                                                        flags,
                                                        tempDevice,
                                                        tempContext,
                                                        featureLevel);

                if (SUCCEEDED (hr))
                {
                    createdDevice = std::move (tempDevice);
                    createdContext = std::move (tempContext);
                    createdFeatureLevel = featureLevel;

                    if (adapterDescription != nullptr)
                        activeAdapterDescription = *adapterDescription;

                    const auto successMessage = String::formatted ("D3D11CreateDevice succeeded (%s, feature=%s, flags=0x%08X%s)",
                                                                    driverName,
                                                                    featureLevelToString (featureLevel),
                                                                    static_cast<unsigned int> (flags),
                                                                    debugSuffix);
                    logInfo (successMessage);
                    appendDiagnostic ("info", successMessage);

                    errorOut.clear();
                    return true;
                }

                const auto failure = describeFailure (driverName, flags, hr);
                logWarning (failure);
                appendDiagnostic ("warning", failure);

                if (combinedErrors.isNotEmpty())
                    combinedErrors += "; ";

                combinedErrors += failure;
            }

            errorOut = combinedErrors;
            return false;
        };

        const auto createDevice = [&] (D3D_DRIVER_TYPE driverType, const char* driverName, String& errorOut) -> bool
        {
            return createDeviceCommon (nullptr, driverType, driverName, errorOut, nullptr);
        };

        const auto createDeviceForAdapter = [&] (IDXGIAdapter1* adapter,
                                                 const DXGI_ADAPTER_DESC1& desc,
                                                 String& errorOut) -> bool
        {
            const auto description = String (desc.Description);
            return createDeviceCommon (adapter,
                                       D3D_DRIVER_TYPE_UNKNOWN,
                                       description.toRawUTF8(),
                                       errorOut,
                                       &description);
        };

        const char* activeDriver = "hardware";
        String hardwareError;

        const auto tryEnumeratedAdapters = [&, this]() -> bool
        {
            for (const auto& info : availableAdapters)
            {
                const auto attemptMessage = String::formatted ("Attempting D3D11CreateDevice on adapter '%s'",
                                            String (info.desc.Description).toRawUTF8());
                logInfo (attemptMessage);
                appendDiagnostic ("info", attemptMessage);

                String adapterError;
                if (createDeviceForAdapter (info.adapter.Get(), info.desc, adapterError))
                {
                    activeDriver = "hardware";
                    hardwareError.clear();
                    return true;
                }

                if (adapterError.isNotEmpty())
                {
                    logWarning (String ("Adapter initialisation failed: ") + adapterError);
                    appendDiagnostic ("warning", String ("Adapter initialisation failed: ") + adapterError);
                    if (hardwareError.isNotEmpty())
                        hardwareError += "; ";

                    hardwareError += adapterError;
                }
            }

            return false;
        };

        bool adapterSuccess = false;

        if (! forceWarp)
            adapterSuccess = tryEnumeratedAdapters();
        else
        {
            hardwareError = "Hardware device initialisation skipped via YUP_RIVE_FORCE_WARP";
            logInfo (hardwareError);
            appendDiagnostic ("info", hardwareError);
        }

        bool hardwareCreated = false;

        if (! adapterSuccess && ! forceWarp)
        {
            const auto hardwareAttempt = String ("Attempting D3D11CreateDevice with hardware driver (no adapter binding)");
            logInfo (hardwareAttempt);
            appendDiagnostic ("info", hardwareAttempt);

            if (! createDevice (D3D_DRIVER_TYPE_HARDWARE, "hardware", hardwareError))
            {
                if (hardwareError.isNotEmpty())
                {
                    const auto hardwareWarning = String ("Hardware device initialisation failed: ") + hardwareError;
                    logWarning (hardwareWarning);
                    appendDiagnostic ("warning", hardwareWarning);
                }
            }
            else
            {
                hardwareCreated = true;
            }
        }

        if (! adapterSuccess && (forceWarp || ! hardwareCreated))
        {
            const auto warpAttempt = String ("Attempting D3D11CreateDevice with WARP software driver");
            logInfo (warpAttempt);
            appendDiagnostic ("info", warpAttempt);

            String warpError;

            if (! createDevice (D3D_DRIVER_TYPE_WARP, "WARP", warpError))
            {
                lastError = hardwareError;

                if (warpError.isNotEmpty())
                {
                    if (lastError.isNotEmpty())
                        lastError += "; ";

                    lastError += warpError;
                }

                logWarning (String ("Unable to create Direct3D11 device: ") + lastError);
                appendDiagnostic ("error", lastError);
                return;
            }

            activeDriver = "WARP";
        }

        if (! adapterSuccess && hardwareError.isNotEmpty())
        {
            appendDiagnostic ("warning", String ("Hardware device initialisation fallback: ") + hardwareError);
        }
        device = std::move (createdDevice);
        deviceContext = std::move (createdContext);
        recordAdapterDetails (device.Get(), activeDriver, createdFeatureLevel);

        try
        {
            rive::gpu::D3DContextOptions contextOptions;
            logInfo ("Creating Rive RenderContextD3DImpl");
            renderContext = rive::gpu::RenderContextD3DImpl::MakeContext (device, deviceContext, contextOptions);

            if (renderContext == nullptr)
            {
                lastError = "Unable to create Rive render context";
                logWarning (lastError);
                appendDiagnostic ("error", lastError);
                releaseDeviceResources();
                return;
            }

            logInfo ("Rive GPU render context created");

            auto* renderContextImpl = renderContext->static_impl_cast<rive::gpu::RenderContextD3DImpl>();
            renderTarget = renderContextImpl->makeRenderTarget (static_cast<uint32_t> (width), static_cast<uint32_t> (height));

            if (! renderTarget)
            {
                lastError = "Unable to create render target";
                logWarning (lastError);
                releaseDeviceResources();
                return;
            }

            auto desc = makeTextureDescription (static_cast<UINT> (width), static_cast<UINT> (height), D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0);
            auto hr = device->CreateTexture2D (&desc, nullptr, renderTexture.GetAddressOf());
            if (FAILED (hr))
            {
                lastError = String::formatted (
                    "CreateTexture2D (render target) failed (0x%08X): %s",
                    static_cast<unsigned int> (hr),
                    makeErrorMessage (hr).c_str());
                logWarning (lastError);
                appendDiagnostic ("error", lastError);
                releaseDeviceResources();
                return;
            }

            desc = makeTextureDescription (static_cast<UINT> (width), static_cast<UINT> (height), D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ);

            for (auto& texture : stagingTextures)
            {
                hr = device->CreateTexture2D (&desc, nullptr, texture.GetAddressOf());
                if (FAILED (hr))
                {
                    lastError = String::formatted (
                        "CreateTexture2D (staging) failed (0x%08X): %s",
                        static_cast<unsigned int> (hr),
                        makeErrorMessage (hr).c_str());
                    logWarning (lastError);
                    appendDiagnostic ("error", lastError);
                    releaseDeviceResources();
                    return;
                }
            }

            {
                std::scoped_lock lock (frameMutex);
                readyFrames.clear();
                std::fill (frameStates.begin(), frameStates.end(), FrameState::Available);
                frameSnapshotDirty = false;
                nextWriteIndex = 0;
            }

            renderer = std::make_unique<rive::RiveRenderer> (renderContext.get());

            bool presentationOk = true;
            if (presentationRequested)
                presentationOk = initialisePresentationResources();

            initialised = true;

            if (presentationOk)
                logInfo (String::formatted ("Rive offscreen renderer ready (presentation %s)", presentationEnabled ? "enabled" : "disabled"));
            else
                logWarning (String ("Presentation initialisation failed: ") + lastError);
        }
        catch (const std::exception& exc)
        {
            lastError = String ("Failed to initialise Rive renderer: ") + exc.what();
            logWarning (lastError);
            appendDiagnostic ("error", lastError);
            releaseDeviceResources();
        }
        catch (...)
        {
            lastError = "Failed to initialise Rive renderer due to an unknown error";
            logWarning (lastError);
            appendDiagnostic ("error", lastError);
            releaseDeviceResources();
        }
    }
    std::vector<AdapterInfo> availableAdapters;
    void releaseDeviceResources()
    {
        releasePresentationResources();

        renderer.reset();
        renderTarget.reset();
        renderContext.reset();
        renderTexture.Reset();
        deviceContext.Reset();
        device.Reset();

        for (auto& texture : stagingTextures)
            texture.Reset();

        {
            std::scoped_lock lock (frameMutex);
            readyFrames.clear();
            frameSnapshot.reset();
            frameSnapshotDirty = false;
            std::fill (frameStates.begin(), frameStates.end(), FrameState::Available);
        }

        activeAdapterDescription = {};
        initialised = false;
        presentationEnabled = false;
    }

    void resetScenes()
    {
        scene = nullptr;
        animation.reset();
        stateMachine.reset();
        sceneHolder.reset();

        if (artboard != nullptr)
            sceneHolder = artboard->defaultScene();

        if (sceneHolder == nullptr && artboard != nullptr)
            sceneHolder = std::make_unique<rive::StaticScene> (artboard.get());

        scene = sceneHolder.get();

        if (scene != nullptr)
        {
            if (dynamic_cast<rive::StateMachineInstance*> (scene) != nullptr)
            {
                stateMachine.reset (static_cast<rive::StateMachineInstance*> (sceneHolder.release()));
                scene = stateMachine.get();
            }
        }
    }

    void updateViewTransform()
    {
        if (artboard == nullptr)
        {
            viewTransform = rive::Mat2D();
            return;
        }

        rive::AABB targetBounds { 0.0f, 0.0f, static_cast<float> (width), static_cast<float> (height) };
        const auto artboardBounds = artboard->bounds();
        viewTransform = rive::computeAlignment (rive::Fit::contain, rive::Alignment::center, targetBounds, artboardBounds);
    }

    std::size_t findAvailableIndex()
    {
        for (std::size_t offset = 0; offset < stagingBufferCount; ++offset)
        {
            const auto index = (nextWriteIndex + offset) % stagingBufferCount;
            if (frameStates[index] == FrameState::Available)
                return index;
        }

        return stagingBufferCount;
    }

    std::size_t acquireWriteIndex()
    {
        std::unique_lock lock (frameMutex);

        while (true)
        {
            const auto available = findAvailableIndex();
            if (available < stagingBufferCount)
            {
                frameStates[available] = FrameState::Writing;
                nextWriteIndex = (available + 1) % stagingBufferCount;
                return available;
            }

            if (! readyFrames.empty())
            {
                const auto dropped = readyFrames.front();
                readyFrames.pop_front();
                frameStates[dropped] = FrameState::Available;
                frameSnapshotDirty = ! readyFrames.empty();
                continue;
            }

            frameCondition.wait (lock);
        }
    }

    bool renderFrame()
    {
        pumpWindowMessages();

        if (! initialised || scene == nullptr)
            return false;

        const auto writeIndex = acquireWriteIndex();

        rive::gpu::RenderContext::FrameDescriptor frameDescriptor = makeFrameDescriptor (width, height);
        renderContext->beginFrame (frameDescriptor);

        renderTarget->setTargetTexture (renderTexture.Get());

        renderer->save();
        renderer->transform (viewTransform);
        scene->draw (renderer.get());
        renderer->restore();

        rive::gpu::RenderContext::FlushResources flushDescriptor {};
        flushDescriptor.renderTarget = renderTarget.get();
        renderContext->flush (flushDescriptor);

        renderTarget->setTargetTexture (nullptr);

        auto* stagingTexture = stagingTextures[writeIndex].Get();
        deviceContext->CopyResource (stagingTexture, renderTexture.Get());

        presentToSwapChain();

        D3D11_MAPPED_SUBRESOURCE mapped {};
        auto hr = deviceContext->Map (stagingTexture, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED (hr))
        {
            lastError = describeMapFailure (hr);

            std::unique_lock lock (frameMutex);
            frameStates[writeIndex] = FrameState::Available;
            frameCondition.notify_one();
            return false;
        }

        auto* srcBytes = static_cast<const uint8*> (mapped.pData);
        auto& destination = stagingBuffers[writeIndex];

        for (int row = 0; row < height; ++row)
        {
            const auto srcRow = srcBytes + static_cast<std::size_t> (row) * mapped.RowPitch;
            auto* dstRow = destination.data() + static_cast<std::size_t> (row) * rowStride;
            std::memcpy (dstRow, srcRow, rowStride);
        }

        deviceContext->Unmap (stagingTexture, 0);

        {
            std::unique_lock lock (frameMutex);
            frameStates[writeIndex] = FrameState::PendingRead;
            readyFrames.push_back (writeIndex);
            frameSnapshotDirty = true;
        }

        frameCondition.notify_one();
        return true;
    }


    void pumpWindowMessages()
    {
        if (! presentationEnabled || presentation.hwnd == nullptr)
            return;

        MSG msg {};
        while (::PeekMessageW (&msg, nullptr, 0u, 0u, PM_REMOVE))
        {
            ::TranslateMessage (&msg);
            ::DispatchMessageW (&msg);
        }
    }

    bool initialisePresentationResources()
    {
        if (! presentationRequested)
            return false;

        if (presentationEnabled)
            return true;

        lastError.clear();

        if (device == nullptr || deviceContext == nullptr)
        {
            lastError = "Presentation requires an initialised D3D11 device";
            logWarning (lastError);
            return false;
        }

        const auto atom = registerWindowClass();
        if (atom == 0)
        {
            const auto errorCode = ::GetLastError();
            lastError = String::formatted ("RegisterClassEx failed (0x%08X)", static_cast<unsigned int> (errorCode));
            logWarning (lastError);
            return false;
        }

        if (presentation.hwnd == nullptr)
        {
            RECT rect { 0, 0, width, height };
            ::AdjustWindowRectEx (&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, 0);

            const auto windowWidth = rect.right - rect.left;
            const auto windowHeight = rect.bottom - rect.top;

            auto hwnd = ::CreateWindowExW (0,
                                           kPresentationWindowClassName,
                                           kPresentationWindowTitle,
                                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                           CW_USEDEFAULT,
                                           CW_USEDEFAULT,
                                           windowWidth,
                                           windowHeight,
                                           nullptr,
                                           nullptr,
                                           ::GetModuleHandleW (nullptr),
                                           this);

            if (hwnd == nullptr)
            {
                const auto errorCode = ::GetLastError();
                lastError = String::formatted ("CreateWindowEx failed (0x%08X)", static_cast<unsigned int> (errorCode));
                logWarning (lastError);
                return false;
            }

            presentation.hwnd = hwnd;
            ::ShowWindow (hwnd, SW_SHOWNORMAL);
            ::UpdateWindow (hwnd);
        }

        Microsoft::WRL::ComPtr<IDXGIDevice1> dxgiDevice;
        auto hr = device.As (&dxgiDevice);
        if (FAILED (hr))
        {
            lastError = String::formatted (
                "IDXGIDevice query failed (0x%08X): %s",
                static_cast<unsigned int> (hr),
                makeErrorMessage (hr).c_str());
            logWarning (lastError);
            releasePresentationResources();
            return false;
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter (adapter.GetAddressOf());
        if (FAILED (hr))
        {
            lastError = String::formatted (
                "IDXGIAdapter query failed (0x%08X): %s",
                static_cast<unsigned int> (hr),
                makeErrorMessage (hr).c_str());
            logWarning (lastError);
            releasePresentationResources();
            return false;
        }

        Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
        hr = adapter->GetParent (__uuidof (IDXGIFactory2), reinterpret_cast<void**> (factory.GetAddressOf()));
        if (FAILED (hr))
        {
            lastError = String::formatted (
                "IDXGIFactory2 query failed (0x%08X): %s",
                static_cast<unsigned int> (hr),
                makeErrorMessage (hr).c_str());
            logWarning (lastError);
            releasePresentationResources();
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 swapChainDesc {};
        swapChainDesc.Width = static_cast<UINT> (width);
        swapChainDesc.Height = static_cast<UINT> (height);
        swapChainDesc.Format = kRenderFormat;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
        swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        hr = factory->CreateSwapChainForHwnd (device.Get(),
                                              presentation.hwnd,
                                              &swapChainDesc,
                                              nullptr,
                                              nullptr,
                                              presentation.swapChain.GetAddressOf());
        if (FAILED (hr))
        {
            lastError = String::formatted (
                "CreateSwapChainForHwnd failed (0x%08X): %s",
                static_cast<unsigned int> (hr),
                makeErrorMessage (hr).c_str());
            logWarning (lastError);
            releasePresentationResources();
            return false;
        }

        factory->MakeWindowAssociation (presentation.hwnd, DXGI_MWA_NO_ALT_ENTER);

        hr = presentation.swapChain->GetBuffer (0, IID_PPV_ARGS (presentation.backBuffer.ReleaseAndGetAddressOf()));
        if (FAILED (hr))
        {
            lastError = String::formatted (
                "Swap chain GetBuffer failed (0x%08X): %s",
                static_cast<unsigned int> (hr),
                makeErrorMessage (hr).c_str());
            logWarning (lastError);
            releasePresentationResources();
            return false;
        }

        hr = device->CreateRenderTargetView (presentation.backBuffer.Get(), nullptr, presentation.renderTargetView.ReleaseAndGetAddressOf());
        if (FAILED (hr))
        {
            lastError = String::formatted (
                "CreateRenderTargetView failed (0x%08X): %s",
                static_cast<unsigned int> (hr),
                makeErrorMessage (hr).c_str());
            logWarning (lastError);
            releasePresentationResources();
            return false;
        }

        presentationEnabled = true;
        lastError.clear();
        logInfo ("Presentation window initialised; swap-chain presentation enabled");
        return true;
    }

    void releasePresentationResources()
    {
        if (presentation.swapChain != nullptr)
        {
            presentation.swapChain->SetFullscreenState (FALSE, nullptr);
            presentation.renderTargetView.Reset();
            presentation.backBuffer.Reset();
            presentation.swapChain.Reset();
        }

        if (presentation.hwnd != nullptr)
        {
            auto hwnd = presentation.hwnd;
            presentation.hwnd = nullptr;
            ::DestroyWindow (hwnd);
        }

        presentationEnabled = false;
    }

    void presentToSwapChain()
    {
        if (! presentationEnabled || presentation.swapChain == nullptr || presentation.backBuffer == nullptr)
            return;

        deviceContext->CopyResource (presentation.backBuffer.Get(), renderTexture.Get());

        auto hr = presentation.swapChain->Present (1, 0);
        if (FAILED (hr))
        {
            lastError = String::formatted (
                "Swap chain Present failed (0x%08X): %s",
                static_cast<unsigned int> (hr),
                makeErrorMessage (hr).c_str());
            logWarning (lastError);

            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            {
                const auto reason = device->GetDeviceRemovedReason();
                logWarning (String::formatted (
                    "Device removed (0x%08X): %s",
                    static_cast<unsigned int> (reason),
                    makeErrorMessage (reason).c_str()));
            }

            releasePresentationResources();
        }
    }

    void recordAdapterDetails (ID3D11Device* deviceToDescribe, const char* driverName, D3D_FEATURE_LEVEL featureLevel)
    {
        currentFeatureLevel = featureLevel;
        activeAdapterDescription = {};

        if (deviceToDescribe == nullptr)
            return;

        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        auto hr = deviceToDescribe->QueryInterface (IID_PPV_ARGS (dxgiDevice.GetAddressOf()));
        if (FAILED (hr))
        {
            logWarning (String::formatted (
                "IDXGIDevice query failed (0x%08X): %s",
                static_cast<unsigned int> (hr),
                makeErrorMessage (hr).c_str()));
            return;
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter (adapter.GetAddressOf());
        if (FAILED (hr))
        {
            logWarning (String::formatted (
                "IDXGIAdapter query failed (0x%08X): %s",
                static_cast<unsigned int> (hr),
                makeErrorMessage (hr).c_str()));
            return;
        }

        DXGI_ADAPTER_DESC desc {};
        hr = adapter->GetDesc (&desc);
        if (FAILED (hr))
        {
            logWarning (String::formatted (
                "IDXGIAdapter::GetDesc failed (0x%08X): %s",
                static_cast<unsigned int> (hr),
                makeErrorMessage (hr).c_str()));
            return;
        }

        activeAdapterDescription = String (desc.Description);

        const double vramGiB = static_cast<double> (desc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0);
        const auto adapterMessage = String::formatted ("Using %s driver on adapter '%s' (vendor=0x%04X, device=0x%04X, VRAM=%.2f GiB, feature=%s)",
            driverName,
            activeAdapterDescription.toRawUTF8(),
            static_cast<unsigned int> (desc.VendorId),
            static_cast<unsigned int> (desc.DeviceId),
            vramGiB,
            featureLevelToString (featureLevel));
        logInfo (adapterMessage);
        appendDiagnostic ("info", adapterMessage);
    }

    static ATOM registerWindowClass()
    {
        static std::once_flag once;
        static ATOM atom = 0;

        std::call_once (once, []()
        {
            WNDCLASSEXW cls {};
            cls.cbSize = sizeof (cls);
            cls.style = CS_HREDRAW | CS_VREDRAW;
            cls.lpfnWndProc = &RiveOffscreenRenderer::Impl::windowProc;
            cls.hInstance = ::GetModuleHandleW (nullptr);
            cls.hCursor = ::LoadCursorW (nullptr, IDC_ARROW);
            cls.lpszClassName = kPresentationWindowClassName;
            atom = ::RegisterClassExW (&cls);
        });

        return atom;
    }

    static LRESULT CALLBACK windowProc (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<CREATESTRUCTW*> (lParam);
            ::SetWindowLongPtrW (hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR> (create->lpCreateParams));
            return TRUE;
        }

        auto* self = reinterpret_cast<Impl*> (::GetWindowLongPtrW (hwnd, GWLP_USERDATA));
        if (self == nullptr)
            return ::DefWindowProcW (hwnd, message, wParam, lParam);

        switch (message)
        {
            case WM_CLOSE:
                self->presentationRequested = false;
                self->releasePresentationResources();
                return 0;

            case WM_DESTROY:
                ::SetWindowLongPtrW (hwnd, GWLP_USERDATA, 0);
                return 0;

            default:
                break;
        }

        return ::DefWindowProcW (hwnd, message, wParam, lParam);
    }

    static const char* featureLevelToString (D3D_FEATURE_LEVEL level)
    {
        switch (level)
        {
            case D3D_FEATURE_LEVEL_11_1: return "11_1";
            case D3D_FEATURE_LEVEL_11_0: return "11_0";
            case D3D_FEATURE_LEVEL_10_1: return "10_1";
            case D3D_FEATURE_LEVEL_10_0: return "10_0";
            case D3D_FEATURE_LEVEL_9_3:  return "9_3";
            case D3D_FEATURE_LEVEL_9_2:  return "9_2";
            case D3D_FEATURE_LEVEL_9_1:  return "9_1";
            default: return "unknown";
        }
    }

    void logInfo (const String& message) const
    {
        appendDiagnostic ("info", message);

        const auto output = String ("[RiveOffscreenRenderer] ") + message;
        Logger::writeToLog (output);
        std::cerr << output.toStdString() << std::endl;
    }

    void logWarning (const String& message) const
    {
        appendDiagnostic ("warning", message);

        const auto output = String ("[RiveOffscreenRenderer] WARNING: ") + message;
        Logger::writeToLog (output);
        std::cerr << output.toStdString() << std::endl;
    }

    mutable String diagnosticsLog;
    mutable std::mutex diagnosticsMutex;

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> renderTexture;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> stagingTextures;

    std::unique_ptr<rive::gpu::RenderContext> renderContext;
    rive::rcp<rive::gpu::RenderTargetD3D> renderTarget;
    std::unique_ptr<rive::RiveRenderer> renderer;

    std::vector<std::vector<uint8>> stagingBuffers;
    mutable std::vector<FrameState> frameStates;
    mutable std::deque<std::size_t> readyFrames;
    std::size_t frameSize = 0;
    std::size_t stagingBufferCount = 1;
    std::size_t nextWriteIndex = 0;
    mutable std::shared_ptr<std::vector<uint8>> frameSnapshot;
    mutable bool frameSnapshotDirty = false;
    mutable std::mutex frameMutex;
    mutable std::condition_variable frameCondition;

    std::shared_ptr<ArtboardFile> artboardFile;
    std::unique_ptr<rive::ArtboardInstance> artboard;
    std::unique_ptr<rive::Scene> sceneHolder;
    std::unique_ptr<rive::LinearAnimationInstance> animation;
    std::unique_ptr<rive::StateMachineInstance> stateMachine;
    rive::Scene* scene = nullptr;

    rive::Mat2D viewTransform = rive::Mat2D();

    String lastError;
    String activeArtboardName;

    PresentationResources presentation;
    bool presentationRequested = false;
    bool presentationEnabled = false;
    D3D_FEATURE_LEVEL currentFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    String activeAdapterDescription;

    int width = 0;
    int height = 0;
    std::size_t rowStride = 0;

    bool initialised = false;
    bool paused = false;

    std::shared_ptr<std::vector<uint8>> ensureFrameSnapshot() const
    {
        std::size_t frameIndex = stagingBufferCount;
        std::shared_ptr<std::vector<uint8>> snapshot;

        {
            std::unique_lock lock (frameMutex);

            if (! frameSnapshotDirty && frameSnapshot != nullptr)
                return frameSnapshot;

            if (readyFrames.empty())
            {
                frameSnapshotDirty = false;

                if (frameSnapshot == nullptr)
                    frameSnapshot = std::make_shared<std::vector<uint8>> (frameSize, 0);

                return frameSnapshot;
            }

            frameIndex = readyFrames.front();
            readyFrames.pop_front();
            frameStates[frameIndex] = FrameState::Reading;
            snapshot = frameSnapshot;
        }

        const auto& source = stagingBuffers[frameIndex];

        if (snapshot != nullptr && snapshot.use_count() == 1 && snapshot->size() == source.size())
        {
            std::copy (source.begin(), source.end(), snapshot->begin());
        }
        else
        {
            snapshot = std::make_shared<std::vector<uint8>> (source);
        }

        {
            std::unique_lock lock (frameMutex);
            frameSnapshot = snapshot;
            frameSnapshotDirty = ! readyFrames.empty();
            frameStates[frameIndex] = FrameState::Available;
        }

        frameCondition.notify_one();
        return snapshot;
    }

    Result setActiveArtboard (std::unique_ptr<rive::ArtboardInstance> newArtboard)
    {
        if (newArtboard == nullptr)
            return Result::fail ("Artboard instance is invalid");

        artboard = std::move (newArtboard);
        activeArtboardName = String (artboard->name());

        updateViewTransform();
        resetScenes();

        if (scene == nullptr)
            return Result::fail ("Artboard does not contain a playable scene");

        paused = false;

        {
            std::scoped_lock lock (frameMutex);
            readyFrames.clear();
            std::fill (frameStates.begin(), frameStates.end(), FrameState::Available);
            nextWriteIndex = 0;
            frameSnapshot.reset();
            frameSnapshotDirty = true;
        }

        scene->advanceAndApply (0.0f);
        if (! renderFrame())
            return Result::fail (lastError);

        const auto animationCount = artboard->animationCount();
        const auto stateMachineCount = artboard->stateMachineCount();
        logInfo (String::formatted ("Activated artboard '%s' (%u animation(s), %u state machine(s))",
                                    activeArtboardName.toRawUTF8(),
                                    static_cast<unsigned int> (animationCount),
                                    static_cast<unsigned int> (stateMachineCount)));

        return Result::ok();
    }

};

} // namespace yup

yup::String yup::RiveOffscreenRenderer::Impl::describeMapFailure (HRESULT hr)
{
    const auto message = makeErrorMessage (hr);
    if (! message.empty())
    {
        return String::formatted (
            "ID3D11DeviceContext::Map failed (0x%08X): %s",
            static_cast<unsigned int> (hr),
            message.c_str());
    }

    return String::formatted (
        "ID3D11DeviceContext::Map failed (0x%08X)",
        static_cast<unsigned int> (hr));
}

#else

namespace yup
{

struct RiveOffscreenRenderer::Impl
{
    Impl (int widthIn, int heightIn, std::size_t stagingBufferCountIn, bool enablePresentation)
        : width (std::max (widthIn, 0)),
          height (std::max (heightIn, 0)),
          rowStride (static_cast<std::size_t> (std::max (widthIn, 0)) * 4u),
          frameSize (rowStride * static_cast<std::size_t> (std::max (heightIn, 0))),
          stagingBufferCount (std::max<std::size_t> (std::size_t { 1 }, stagingBufferCountIn)),
          frameSnapshot (std::make_shared<std::vector<uint8>> (frameSize, 0))
    {
        if (widthIn <= 0 || heightIn <= 0)
        {
            lastError = String::formatted (
                "Renderer dimensions must be positive (received %dx%d)",
                widthIn,
                heightIn);
        }
    }

    void setPresentationEnabled (bool shouldEnable)
    {
        presentationRequested = shouldEnable;
        presentationEnabled = false;

        if (shouldEnable)
            lastError = "Presentation mode is only available on Windows";
        else
            lastError.clear();
    }

    bool isPresentationEnabled() const noexcept { return presentationEnabled; }

    Result load (const File&, const String&)
    {
        lastError = "Direct3D11 offscreen rendering is only available on Windows";
        return Result::fail (lastError);
    }

    Result load (Span<const uint8> /*bytes*/, const String&)
    {
        lastError = "Direct3D11 offscreen rendering is only available on Windows";
        return Result::fail (lastError);
    }

    StringArray listArtboards() const { return {}; }
    StringArray listAnimations() const { return {}; }
    StringArray listStateMachines() const { return {}; }
    bool playAnimation (const String&, bool) { return false; }
    bool playStateMachine (const String&) { return false; }
    void stop() { paused = false; }
    bool setBoolInput (const String&, bool) { return false; }
    bool setNumberInput (const String&, double) { return false; }
    bool fireTrigger (const String&) { return false; }

    bool advance (float)
    {
        if (frameSize == 0)
            return true;

        std::vector<uint8> frame (frameSize, static_cast<uint8> (frameCounter & 0xFFu));
        ++frameCounter;

        {
            std::lock_guard lock (frameMutex);
            readyFrames.push_back (std::move (frame));
            while (readyFrames.size() > stagingBufferCount)
                readyFrames.pop_front();
            frameSnapshotDirty = true;
        }

        return true;
    }

    void setPaused (bool shouldPause) { paused = shouldPause; }
    bool isPaused() const noexcept { return paused; }
    int getWidth() const noexcept { return width; }
    int getHeight() const noexcept { return height; }
    std::size_t getStride() const noexcept { return rowStride; }

    const std::vector<uint8>& getFrameBuffer() const noexcept { return *ensureFrameSnapshot(); }

    std::shared_ptr<const std::vector<uint8>> getFrameBufferShared() const noexcept
    {
        return ensureFrameSnapshot();
    }

    const String& getLastError() const noexcept { return lastError; }
    Result selectArtboard (const String& name)
    {
        (void) name;
        lastError = "Direct3D11 offscreen rendering is only available on Windows";
        return Result::fail (lastError);
    }
    String getActiveArtboardName() const { return {}; }

    std::shared_ptr<std::vector<uint8>> ensureFrameSnapshot() const
    {
        std::lock_guard lock (frameMutex);

        if (frameSnapshotDirty && ! readyFrames.empty())
        {
            auto frame = std::move (readyFrames.front());
            readyFrames.pop_front();

            if (frameSnapshot != nullptr && frameSnapshot.use_count() == 1 && frameSnapshot->size() == frame.size())
            {
                *frameSnapshot = std::move (frame);
            }
            else
            {
                frameSnapshot = std::make_shared<std::vector<uint8>> (std::move (frame));
            }

            frameSnapshotDirty = ! readyFrames.empty();
        }

        if (frameSnapshot == nullptr)
            frameSnapshot = std::make_shared<std::vector<uint8>> (frameSize, 0);

        return frameSnapshot;
    }

    int width = 0;
    int height = 0;
    std::size_t rowStride = 0;
    std::size_t frameSize = 0;
    std::size_t stagingBufferCount = 1;
    mutable std::shared_ptr<std::vector<uint8>> frameSnapshot;
    mutable bool frameSnapshotDirty = false;
    mutable std::mutex frameMutex;
    mutable std::deque<std::vector<uint8>> readyFrames;
    std::size_t frameCounter = 0;
    String lastError;
    bool paused = false;
};

} // namespace yup

#endif

namespace yup
{

RiveOffscreenRenderer::RiveOffscreenRenderer (int width, int height, std::size_t stagingBufferCount, bool enablePresentation)
    : impl (std::make_unique<Impl> (width, height, stagingBufferCount, enablePresentation))
{
}

RiveOffscreenRenderer::~RiveOffscreenRenderer() = default;

void RiveOffscreenRenderer::setPresentationEnabled (bool shouldEnable)
{
    impl->setPresentationEnabled (shouldEnable);
}

bool RiveOffscreenRenderer::isPresentationEnabled() const noexcept
{
    return impl->isPresentationEnabled();
}

bool RiveOffscreenRenderer::isValid() const noexcept
{
    return impl->isValid();
}

Result RiveOffscreenRenderer::load (const File& file, const String& artboardName)
{
    return impl->load (file, artboardName);
}

Result RiveOffscreenRenderer::loadFromBytes (Span<const uint8> bytes, const String& artboardName)
{
    return impl->load (bytes, artboardName);
}

Result RiveOffscreenRenderer::loadFromBytes (const std::vector<uint8>& bytes, const String& artboardName)
{
    return loadFromBytes (Span<const uint8> (static_cast<const uint8*> (bytes.data()), bytes.size()), artboardName);
}

StringArray RiveOffscreenRenderer::listArtboards() const
{
    return impl->listArtboards();
}

StringArray RiveOffscreenRenderer::listAnimations() const
{
    return impl->listAnimations();
}

StringArray RiveOffscreenRenderer::listStateMachines() const
{
    return impl->listStateMachines();
}

bool RiveOffscreenRenderer::playAnimation (const String& animationName, bool shouldLoop)
{
    return impl->playAnimation (animationName, shouldLoop);
}

bool RiveOffscreenRenderer::playStateMachine (const String& machineName)
{
    return impl->playStateMachine (machineName);
}

Result RiveOffscreenRenderer::selectArtboard (const String& artboardName)
{
    return impl->selectArtboard (artboardName);
}

void RiveOffscreenRenderer::stop()
{
    impl->stop();
}

void RiveOffscreenRenderer::setPaused (bool shouldPause)
{
    impl->setPaused (shouldPause);
}

bool RiveOffscreenRenderer::isPaused() const noexcept
{
    return impl->isPaused();
}

bool RiveOffscreenRenderer::setBoolInput (const String& name, bool value)
{
    return impl->setBoolInput (name, value);
}

bool RiveOffscreenRenderer::setNumberInput (const String& name, double value)
{
    return impl->setNumberInput (name, value);
}

bool RiveOffscreenRenderer::fireTriggerInput (const String& name)
{
    return impl->fireTrigger (name);
}

bool RiveOffscreenRenderer::advance (float deltaSeconds)
{
    return impl->advance (deltaSeconds);
}

int RiveOffscreenRenderer::getWidth() const noexcept
{
    return impl->getWidth();
}

int RiveOffscreenRenderer::getHeight() const noexcept
{
    return impl->getHeight();
}

std::size_t RiveOffscreenRenderer::getRowStride() const noexcept
{
    return impl->getStride();
}

const std::vector<uint8>& RiveOffscreenRenderer::getFrameBuffer() const noexcept
{
    return impl->getFrameBuffer();
}

std::shared_ptr<const std::vector<uint8>> RiveOffscreenRenderer::getFrameBufferShared() const noexcept
{
    return impl->getFrameBufferShared();
}

const String& RiveOffscreenRenderer::getLastError() const noexcept
{
    return impl->getLastError();
}

String RiveOffscreenRenderer::getDiagnostics() const
{
    return impl->getDiagnosticsReport();
}

String RiveOffscreenRenderer::getActiveArtboardName() const
{
    return impl->getActiveArtboardName();
}

} // namespace yup

