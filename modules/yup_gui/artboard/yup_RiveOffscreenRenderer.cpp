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

#include <array>
#include <cstring>
#include <functional>
#include <optional>
#include <mutex>

#if YUP_WINDOWS && YUP_RIVE_USE_D3D

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include "rive/layout.hpp"
#include "rive/animation/linear_animation_instance.hpp"
#include "rive/animation/state_machine_instance.hpp"
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
    explicit Impl (int widthIn, int heightIn)
        : width (widthIn),
          height (heightIn),
          rowStride (static_cast<std::size_t> (widthIn) * 4),
          frameBufferWrite (static_cast<std::size_t> (widthIn) * static_cast<std::size_t> (heightIn) * 4u, 0)
    {
        initialise();
    }

    ~Impl() = default;

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

        auto* riveFile = artboardFile->getRiveFile();
        if (riveFile == nullptr)
            return failWith ("Loaded Rive file is invalid");

        std::unique_ptr<rive::ArtboardInstance> loadedArtboard;

        if (artboardName.isNotEmpty())
            loadedArtboard = riveFile->artboardNamed (artboardName.toStdString());
        else
            loadedArtboard = riveFile->artboardDefault();

        if (loadedArtboard == nullptr)
            return failWith ("Unable to create artboard instance");

        artboard = std::move (loadedArtboard);
        updateViewTransform();

        resetScenes();

        if (! scene)
            return failWith ("Artboard does not contain a playable scene");

        paused = false;

        {
            std::scoped_lock lock (frameMutex);
            frameSnapshot.reset();
            frameSnapshotDirty = true;
        }

        scene->advanceAndApply (0.0f);
        renderFrame();
        return Result::ok();
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
        renderFrame();
        return true;
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
        renderFrame();
        return true;
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
        renderFrame();
        return keepAnimating;
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
            lastError = String (makeErrorMessage (hr));
            return;
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
            lastError = String (makeErrorMessage (hr));
            return;
        }

        desc = makeTextureDescription (static_cast<UINT> (width), static_cast<UINT> (height), D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ);
        hr = device->CreateTexture2D (&desc, nullptr, stagingTexture.GetAddressOf());
        if (FAILED (hr))
        {
            lastError = String (makeErrorMessage (hr));
            return;
        }

        renderer = std::make_unique<rive::RiveRenderer> (renderContext.get());
        initialised = true;
    }

    void resetScenes()
    {
        scene = nullptr;
        animation.reset();
        stateMachine.reset();
        sceneHolder.reset();

        if (artboard != nullptr)
            sceneHolder = artboard->defaultScene();

        scene = sceneHolder.get();
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

    void renderFrame()
    {
        if (! initialised || scene == nullptr)
            return;

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

        deviceContext->CopyResource (stagingTexture.Get(), renderTexture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped {};
        auto hr = deviceContext->Map (stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED (hr))
        {
            lastError = String (makeErrorMessage (hr));
            return;
        }

        auto* srcBytes = static_cast<const uint8*> (mapped.pData);

        {
            std::scoped_lock lock (frameMutex);

            for (int row = 0; row < height; ++row)
            {
                const auto srcRow = srcBytes + static_cast<std::size_t> (row) * mapped.RowPitch;
                auto* dstRow = frameBufferWrite.data() + static_cast<std::size_t> (row) * rowStride;
                std::memcpy (dstRow, srcRow, rowStride);
            }

            frameSnapshotDirty = true;
        }

        deviceContext->Unmap (stagingTexture.Get(), 0);
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> renderTexture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;

    std::unique_ptr<rive::gpu::RenderContext> renderContext;
    rive::rcp<rive::gpu::RenderTargetD3D> renderTarget;
    std::unique_ptr<rive::RiveRenderer> renderer;

    std::vector<uint8> frameBufferWrite;
    mutable std::shared_ptr<std::vector<uint8>> frameSnapshot;
    mutable bool frameSnapshotDirty = true;
    mutable std::mutex frameMutex;

    std::shared_ptr<ArtboardFile> artboardFile;
    std::unique_ptr<rive::ArtboardInstance> artboard;
    std::unique_ptr<rive::Scene> sceneHolder;
    std::unique_ptr<rive::LinearAnimationInstance> animation;
    std::unique_ptr<rive::StateMachineInstance> stateMachine;
    rive::Scene* scene = nullptr;

    rive::Mat2D viewTransform = rive::Mat2D::identity();

    String lastError;

    int width = 0;
    int height = 0;
    std::size_t rowStride = 0;

    bool initialised = false;
    bool paused = false;

    std::shared_ptr<std::vector<uint8>> ensureFrameSnapshot() const
    {
        std::scoped_lock lock (frameMutex);

        if (frameSnapshotDirty || frameSnapshot == nullptr)
        {
            if (frameSnapshot != nullptr && frameSnapshot.use_count() == 1 && frameSnapshot->size() == frameBufferWrite.size())
            {
                *frameSnapshot = frameBufferWrite;
            }
            else
            {
                frameSnapshot = std::make_shared<std::vector<uint8>> (frameBufferWrite);
            }

            frameSnapshotDirty = false;
        }

        return frameSnapshot;
    }
};

} // namespace yup

#else

namespace yup
{

struct RiveOffscreenRenderer::Impl
{
    Impl (int widthIn, int heightIn)
        : width (widthIn), height (heightIn)
    {
        frameBuffer = std::make_shared<std::vector<uint8>> (static_cast<std::size_t> (width) * static_cast<std::size_t> (height) * 4u, 0);
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

    StringArray listAnimations() const { return {}; }
    StringArray listStateMachines() const { return {}; }
    bool playAnimation (const String&, bool) { return false; }
    bool playStateMachine (const String&) { return false; }
    void stop() { paused = false; }
    bool setBoolInput (const String&, bool) { return false; }
    bool setNumberInput (const String&, double) { return false; }
    bool fireTrigger (const String&) { return false; }
    bool advance (float) { return false; }
    void setPaused (bool shouldPause) { paused = shouldPause; }
    bool isPaused() const noexcept { return paused; }
    int getWidth() const noexcept { return width; }
    int getHeight() const noexcept { return height; }
    std::size_t getStride() const noexcept { return static_cast<std::size_t> (width) * 4u; }
    const std::vector<uint8>& getFrameBuffer() const noexcept { return *frameBuffer; }
    std::shared_ptr<const std::vector<uint8>> getFrameBufferShared() const noexcept { return frameBuffer; }
    const String& getLastError() const noexcept { return lastError; }

    int width = 0;
    int height = 0;
    std::shared_ptr<std::vector<uint8>> frameBuffer;
    String lastError;
    bool paused = false;
};

} // namespace yup

#endif

namespace yup
{

RiveOffscreenRenderer::RiveOffscreenRenderer (int width, int height)
    : impl (std::make_unique<Impl> (width, height))
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

} // namespace yup

