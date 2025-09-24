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

#include "yup_gui.h"

#include "artboard/yup_RiveOffscreenRenderer.h"

#include "artboard/yup_ArtboardFile.h"
#include "yup_core/streams/yup_MemoryInputStream.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#if YUP_WINDOWS && YUP_RIVE_USE_D3D

#include <d3d11.h>
#include <dxgi1_2.h>
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

    explicit Impl (int widthIn, int heightIn, std::size_t stagingBufferCountIn)
        : width (std::max (widthIn, 0)),
          height (std::max (heightIn, 0)),
          rowStride (static_cast<std::size_t> (std::max (widthIn, 0)) * 4u),
          frameSize (rowStride * static_cast<std::size_t> (std::max (heightIn, 0))),
          stagingBufferCount (std::max<std::size_t> (std::size_t { 1 }, stagingBufferCountIn)),
          stagingTextures (stagingBufferCount),
          stagingBuffers (stagingBufferCount, std::vector<uint8> (frameSize, 0)),
          frameStates (stagingBufferCount, FrameState::Available),
          frameSnapshot (std::make_shared<std::vector<uint8>> (frameSize, 0))
    {
        if (widthIn <= 0 || heightIn <= 0)
        {
            lastError = String::formatted (
                "Renderer dimensions must be positive (received %dx%d)",
                widthIn,
                heightIn);
            return;
        }

        initialise();
    }

    ~Impl() = default;

    static String describeMapFailure (HRESULT hr);

    bool isValid() const noexcept { return initialised; }

    Result load (const File& fileToLoad, const String& artboardName)
    {
        return loadInternal (
            [&fileToLoad] (rive::Factory& factory) { return ArtboardFile::load (fileToLoad, factory); }, artboardName);
    }

    Result load (Span<const uint8> bytes, const String& artboardName)
    {
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
        lastError.clear();

        const auto failWith = [this] (String message)
        {
            lastError = std::move (message);
            return Result::fail (lastError);
        };

        if (! initialised)
            return failWith ("Rive offscreen renderer is not available");

        auto factory = renderContext->factory();
        if (factory == nullptr)
            return failWith ("Missing Rive factory");

        auto loadResult = loader (*factory);
        if (! loadResult)
        {
            lastError = loadResult.getErrorMessage();
            return Result::fail (lastError);
        }

        artboardFile = loadResult.getValue();

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

private:
    void initialise()
    {
        using Microsoft::WRL::ComPtr;

        lastError.clear();

        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if YUP_DEBUG
        creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        const D3D_FEATURE_LEVEL requestedLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

        ComPtr<ID3D11Device> createdDevice;
        ComPtr<ID3D11DeviceContext> createdContext;

        const auto describeFailure = [] (const char* driverName, HRESULT hr)
        {
            const auto message = makeErrorMessage (hr);
            if (! message.empty())
            {
                return String::formatted (
                    "D3D11CreateDevice (%s) failed (0x%08X): %s",
                    driverName,
                    static_cast<unsigned int> (hr),
                    message.c_str());
            }

            return String::formatted (
                "D3D11CreateDevice (%s) failed (0x%08X)",
                driverName,
                static_cast<unsigned int> (hr));
        };

        auto hr = D3D11CreateDevice (nullptr,
                                      D3D_DRIVER_TYPE_HARDWARE,
                                      nullptr,
                                      creationFlags,
                                      requestedLevels,
                                      static_cast<UINT> (std::size (requestedLevels)),
                                      D3D11_SDK_VERSION,
                                      createdDevice.GetAddressOf(),
                                      nullptr,
                                      createdContext.GetAddressOf());

        if (FAILED (hr))
        {
            const auto hardwareError = describeFailure ("hardware", hr);

            hr = D3D11CreateDevice (nullptr,
                                     D3D_DRIVER_TYPE_WARP,
                                     nullptr,
                                     creationFlags,
                                     requestedLevels,
                                     static_cast<UINT> (std::size (requestedLevels)),
                                     D3D11_SDK_VERSION,
                                     createdDevice.GetAddressOf(),
                                     nullptr,
                                     createdContext.GetAddressOf());

            if (FAILED (hr))
            {
                const auto warpError = describeFailure ("WARP", hr);
                lastError = hardwareError;
                lastError += "; ";
                lastError += warpError;
                return;
            }
        }

        device = std::move (createdDevice);
        deviceContext = std::move (createdContext);

        rive::gpu::D3DContextOptions contextOptions;
        renderContext = rive::gpu::RenderContextD3DImpl::MakeContext (device, deviceContext, contextOptions);

        if (renderContext == nullptr)
        {
            lastError = "Unable to create Rive render context";
            return;
        }

        auto* renderContextImpl = renderContext->static_impl_cast<rive::gpu::RenderContextD3DImpl>();
        renderTarget = renderContextImpl->makeRenderTarget (static_cast<uint32_t> (width), static_cast<uint32_t> (height));

        if (! renderTarget)
        {
            lastError = "Unable to create render target";
            return;
        }

        auto desc = makeTextureDescription (static_cast<UINT> (width), static_cast<UINT> (height), D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0);
        hr = device->CreateTexture2D (&desc, nullptr, renderTexture.GetAddressOf());
        if (FAILED (hr))
        {
            lastError = String::formatted (
                "CreateTexture2D (render target) failed (0x%08X): %s",
                static_cast<unsigned int> (hr),
                makeErrorMessage (hr).c_str());
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
        initialised = true;
        lastError.clear();
    }

    static String describeMapFailure (HRESULT hr)
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
            viewTransform = rive::Mat2D::identity();
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

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> renderTexture;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Texture2D>> stagingTextures;

    std::unique_ptr<rive::gpu::RenderContext> renderContext;
    rive::rcp<rive::gpu::RenderTargetD3D> renderTarget;
    std::unique_ptr<rive::RiveRenderer> renderer;

    std::vector<std::vector<uint8>> stagingBuffers;
    std::vector<FrameState> frameStates;
    std::deque<std::size_t> readyFrames;
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

    rive::Mat2D viewTransform = rive::Mat2D::identity();

    String lastError;
    String activeArtboardName;

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

        return Result::ok();
    }

    String getActiveArtboardName() const
    {
        return activeArtboardName;
    }
};

} // namespace yup

#else

namespace yup
{

struct RiveOffscreenRenderer::Impl
{
    Impl (int widthIn, int heightIn, std::size_t stagingBufferCountIn)
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

    bool isValid() const noexcept { return false; }

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

RiveOffscreenRenderer::RiveOffscreenRenderer (int width, int height, std::size_t stagingBufferCount)
    : impl (std::make_unique<Impl> (width, height, stagingBufferCount))
{
}

RiveOffscreenRenderer::~RiveOffscreenRenderer() = default;

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

String RiveOffscreenRenderer::getActiveArtboardName() const
{
    return impl->getActiveArtboardName();
}

} // namespace yup

