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

#include "yup_RiveOffscreenRenderer.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <rive/animation/linear_animation_instance.hpp>
#include <rive/animation/state_machine_input_instance.hpp>
#include <rive/animation/state_machine_instance.hpp>
#include <rive/file.hpp>
#include <rive/math/mat2d.hpp>
#include <rive/renderer/render_context.hpp>
#include <rive/renderer/rive_renderer.hpp>

namespace yup
{
namespace
{
static std::vector<uint8_t> readFileToMemory (const std::string& path)
{
    std::ifstream stream (path, std::ios::binary);
    if (! stream.is_open())
        throw std::runtime_error ("Unable to open Rive file: " + path);

    stream.seekg (0, std::ios::end);
    const auto size = stream.tellg ();
    if (size <= 0)
        return {};

    std::vector<uint8_t> data (static_cast<size_t> (size));
    stream.seekg (0, std::ios::beg);
    stream.read (reinterpret_cast<char*> (data.data()), static_cast<std::streamsize> (size));
    return data;
}

static std::unique_ptr<rive::File> importFile (const std::vector<uint8_t>& bytes, rive::Factory* factory)
{
    if (bytes.empty())
        throw std::runtime_error ("Rive file is empty");

    if (factory == nullptr)
        throw std::runtime_error ("Rive factory is null");

    auto riveFile = rive::File::import (rive::Span<const uint8_t> (bytes.data(), bytes.size()), factory);
    if (riveFile == nullptr)
        throw std::runtime_error ("Failed to import Rive file");

    return riveFile;
}

static std::unique_ptr<rive::ArtboardInstance> makeArtboardInstance (rive::File& file, std::optional<std::string_view> artboardName)
{
    if (artboardName && ! artboardName->empty())
    {
        auto instance = file.artboardNamed (std::string (*artboardName));
        if (instance != nullptr)
            return instance;

        throw std::runtime_error ("Requested artboard not found: " + std::string (*artboardName));
    }

    if (auto instance = file.artboardDefault(); instance != nullptr)
        return instance;

    throw std::runtime_error ("The Rive file does not contain a default artboard");
}

static rive::Mat2D makeCenteredFitTransform (const rive::ArtboardInstance& artboard, int targetWidth, int targetHeight)
{
    const auto artboardWidth = artboard.width();
    const auto artboardHeight = artboard.height();

    if (artboardWidth <= 0.0f || artboardHeight <= 0.0f || targetWidth <= 0 || targetHeight <= 0)
        return rive::Mat2D::identity ();

    const auto scaleX = static_cast<float> (targetWidth) / artboardWidth;
    const auto scaleY = static_cast<float> (targetHeight) / artboardHeight;
    const auto scale = std::min (scaleX, scaleY);

    const auto contentWidth = artboardWidth * scale;
    const auto contentHeight = artboardHeight * scale;

    const auto translateX = 0.5f * (static_cast<float> (targetWidth) - contentWidth);
    const auto translateY = 0.5f * (static_cast<float> (targetHeight) - contentHeight);

    return rive::Mat2D::fromTranslate (translateX, translateY) * rive::Mat2D::fromScale (scale, scale);
}
} // namespace

class RiveOffscreenRenderer::Impl
{
public:
    explicit Impl (Options options)
        : opts (options)
    {
    }

    virtual ~Impl() = default;

    virtual void load (const std::vector<uint8_t>& bytes, std::optional<std::string_view> artboard) = 0;
    virtual std::vector<std::string> animationNames() const = 0;
    virtual std::vector<std::string> stateMachineNames() const = 0;
    virtual bool playAnimation (const std::string& name, bool loop) = 0;
    virtual bool playStateMachine (const std::string& name) = 0;
    virtual void stop() = 0;
    virtual bool setNumberInput (const std::string& name, float value) = 0;
    virtual bool setBooleanInput (const std::string& name, bool value) = 0;
    virtual bool fireTrigger (const std::string& name) = 0;
    virtual bool advance (float deltaSeconds) = 0;
    virtual const std::vector<uint8_t>& pixelBuffer() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual std::size_t stride() const = 0;
    virtual bool hasFrame() const = 0;

protected:
    Options opts;
};

#if YUP_WINDOWS && YUP_RIVE_USE_D3D

#include <rive/renderer/d3d11/render_context_d3d_impl.hpp>

#include <d3d11.h>
#include <dxgi1_2.h>

using Microsoft::WRL::ComPtr;

class RiveOffscreenRendererD3D : public RiveOffscreenRenderer::Impl
{
public:
    explicit RiveOffscreenRendererD3D (Options options)
        : Impl (options)
    {
        initialiseDevice();
        resizeIfNeeded (options.width, options.height);
    }

    void load (const std::vector<uint8_t>& bytes, std::optional<std::string_view> artboard) override
    {
        std::lock_guard lock (mutex);

        auto riveFile = importFile (bytes, renderContext.get());
        artboardInstance = makeArtboardInstance (*riveFile, artboard);
        riveFilePtr = std::move (riveFile);
        resetPlayback();
        renderFrame();
    }

    std::vector<std::string> animationNames() const override
    {
        std::vector<std::string> names;
        if (! artboardInstance)
            return names;

        const auto count = artboardInstance->animationCount();
        names.reserve (count);
        for (size_t i = 0; i < count; ++i)
            names.emplace_back (artboardInstance->animationNameAt (i));
        return names;
    }

    std::vector<std::string> stateMachineNames() const override
    {
        std::vector<std::string> names;
        if (! artboardInstance)
            return names;

        const auto count = artboardInstance->stateMachineCount();
        names.reserve (count);
        for (size_t i = 0; i < count; ++i)
            names.emplace_back (artboardInstance->stateMachineNameAt (i));
        return names;
    }

    bool playAnimation (const std::string& name, bool loop) override
    {
        std::lock_guard lock (mutex);
        if (! artboardInstance)
            return false;

        auto instance = artboardInstance->animationNamed (name);
        if (instance == nullptr)
            return false;

        instance->loopValue (loop ? static_cast<int> (rive::Loop::loop) : static_cast<int> (rive::Loop::oneShot));
        animationInstance = std::move (instance);
        stateMachineInstance.reset();
        return true;
    }

    bool playStateMachine (const std::string& name) override
    {
        std::lock_guard lock (mutex);
        if (! artboardInstance)
            return false;

        auto instance = artboardInstance->stateMachineNamed (name);
        if (instance == nullptr)
            return false;

        stateMachineInstance = std::move (instance);
        animationInstance.reset();
        return true;
    }

    void stop() override
    {
        std::lock_guard lock (mutex);
        animationInstance.reset();
        stateMachineInstance.reset();
    }

    bool setNumberInput (const std::string& name, float value) override
    {
        std::lock_guard lock (mutex);
        if (! stateMachineInstance)
            return false;
        if (auto input = stateMachineInstance->getNumber (name); input != nullptr)
        {
            input->value (value);
            return true;
        }
        return false;
    }

    bool setBooleanInput (const std::string& name, bool value) override
    {
        std::lock_guard lock (mutex);
        if (! stateMachineInstance)
            return false;
        if (auto input = stateMachineInstance->getBool (name); input != nullptr)
        {
            input->value (value);
            return true;
        }
        return false;
    }

    bool fireTrigger (const std::string& name) override
    {
        std::lock_guard lock (mutex);
        if (! stateMachineInstance)
            return false;
        if (auto input = stateMachineInstance->getTrigger (name); input != nullptr)
        {
            input->fire ();
            return true;
        }
        return false;
    }

    bool advance (float deltaSeconds) override
    {
        std::lock_guard lock (mutex);
        if (! artboardInstance)
            return false;

        bool updated = false;
        if (stateMachineInstance)
            updated |= stateMachineInstance->advanceAndApply (deltaSeconds);
        else if (animationInstance)
        {
            const auto continuePlaying = animationInstance->advance (deltaSeconds);
            animationInstance->apply (1.0f);
            updated = true;

            if (! continuePlaying)
            {
                const auto shouldLoop = animationInstance->loopValue() != static_cast<int> (rive::Loop::oneShot);
                if (! shouldLoop)
                    animationInstance.reset();
            }
        }

        updated |= artboardInstance->advance (deltaSeconds);
        renderFrame();
        return updated;
    }

    const std::vector<uint8_t>& pixelBuffer() const override
    {
        return pixelBytes;
    }

    int width() const override { return renderSize.first; }
    int height() const override { return renderSize.second; }
    std::size_t stride() const override { return rowPitch; }
    bool hasFrame() const override { return ! pixelBytes.empty(); }

private:
    void initialiseDevice()
    {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    #ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    #endif

        D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
        D3D_FEATURE_LEVEL obtainedLevel = D3D_FEATURE_LEVEL_11_0;

        VERIFY_OK (D3D11CreateDevice (nullptr,
                                      D3D_DRIVER_TYPE_HARDWARE,
                                      nullptr,
                                      flags,
                                      &featureLevel,
                                      1,
                                      D3D11_SDK_VERSION,
                                      device.ReleaseAndGetAddressOf(),
                                      &obtainedLevel,
                                      deviceContext.ReleaseAndGetAddressOf()));

        rive::gpu::D3DContextOptions contextOptions;
        contextOptions.disableRasterizerOrderedViews = opts.disableRasterOrdering;

        renderContext = rive::gpu::RenderContextD3DImpl::MakeContext (device, deviceContext, contextOptions);
        if (renderContext == nullptr)
            throw std::runtime_error ("Failed to construct Rive D3D render context");

        renderer = std::make_unique<rive::RiveRenderer> (renderContext.get());
        renderTarget = renderContext->static_impl_cast<rive::gpu::RenderContextD3DImpl>()->makeRenderTarget (1, 1);
    }

    void resizeIfNeeded (int width, int height)
    {
        if (width <= 0 || height <= 0)
            return;

        if (renderSize.first == width && renderSize.second == height)
            return;

        renderSize = { width, height };
        pixelBytes.resize (static_cast<size_t> (width) * static_cast<size_t> (height) * 4u);

        auto* contextImpl = renderContext->static_impl_cast<rive::gpu::RenderContextD3DImpl>();
        renderTarget = contextImpl->makeRenderTarget (static_cast<uint32_t> (width), static_cast<uint32_t> (height));

        D3D11_TEXTURE2D_DESC desc {};
        desc.Width = static_cast<UINT> (width);
        desc.Height = static_cast<UINT> (height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_UNORDERED_ACCESS;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;

        VERIFY_OK (device->CreateTexture2D (&desc, nullptr, renderTexture.ReleaseAndGetAddressOf()));

        desc.BindFlags = 0;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        VERIFY_OK (device->CreateTexture2D (&desc, nullptr, stagingTexture.ReleaseAndGetAddressOf()));

        rowPitch = static_cast<size_t> (width) * 4u;
    }

    void resetPlayback()
    {
        animationInstance.reset();
        stateMachineInstance.reset();
    }

    void renderFrame()
    {
        if (! artboardInstance)
            return;

        resizeIfNeeded (opts.width > 0 ? opts.width : static_cast<int> (artboardInstance->width()),
                        opts.height > 0 ? opts.height : static_cast<int> (artboardInstance->height()));

        rive::gpu::RenderContext::FrameDescriptor frameDesc {};
        frameDesc.renderTargetWidth = static_cast<uint32_t> (renderSize.first);
        frameDesc.renderTargetHeight = static_cast<uint32_t> (renderSize.second);
        frameDesc.loadAction = rive::gpu::LoadAction::clear;
        frameDesc.clearColor = 0;
        frameDesc.disableRasterOrdering = opts.disableRasterOrdering;

        renderTarget->setTargetTexture (renderTexture);
        renderContext->beginFrame (frameDesc);

        renderer->save();
        renderer->transform (makeCenteredFitTransform (*artboardInstance, renderSize.first, renderSize.second));
        artboardInstance->draw (renderer.get());
        renderer->restore();

        rive::gpu::RenderContext::FlushResources flush {};
        flush.renderTarget = renderTarget.get();
        renderContext->flush (flush);
        renderTarget->setTargetTexture (nullptr);

        deviceContext->CopyResource (stagingTexture.Get(), renderTexture.Get());

        D3D11_MAPPED_SUBRESOURCE mapped {};
        VERIFY_OK (deviceContext->Map (stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped));

        const auto targetBytes = static_cast<size_t> (renderSize.first) * static_cast<size_t> (renderSize.second) * 4u;
        if (pixelBytes.size() != targetBytes)
            pixelBytes.resize (targetBytes);

        const auto* src = static_cast<const uint8_t*> (mapped.pData);
        auto* dst = pixelBytes.data();

        for (int y = 0; y < renderSize.second; ++y)
        {
            std::memcpy (dst + static_cast<size_t> (y) * rowPitch,
                         src + static_cast<size_t> (y) * mapped.RowPitch,
                         rowPitch);
        }

        deviceContext->Unmap (stagingTexture.Get(), 0);
    }

    mutable std::mutex mutex;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> deviceContext;

    std::unique_ptr<rive::gpu::RenderContext> renderContext;
    std::unique_ptr<rive::RiveRenderer> renderer;
    rive::rcp<rive::gpu::RenderTargetD3D> renderTarget;

    ComPtr<ID3D11Texture2D> renderTexture;
    ComPtr<ID3D11Texture2D> stagingTexture;

    std::unique_ptr<rive::File> riveFilePtr;
    std::unique_ptr<rive::ArtboardInstance> artboardInstance;
    std::unique_ptr<rive::LinearAnimationInstance> animationInstance;
    std::unique_ptr<rive::StateMachineInstance> stateMachineInstance;

    std::pair<int, int> renderSize { 0, 0 };
    std::vector<uint8_t> pixelBytes;
    std::size_t rowPitch = 0;
};

#else

class RiveOffscreenRendererStub : public RiveOffscreenRenderer::Impl
{
public:
    explicit RiveOffscreenRendererStub (Options options)
        : Impl (options)
    {
    }

    void load (const std::vector<uint8_t>&, std::optional<std::string_view>) override
    {
        throw std::runtime_error ("RiveOffscreenRenderer requires Direct3D 11 and is only available on Windows builds");
    }

    std::vector<std::string> animationNames() const override { return {}; }
    std::vector<std::string> stateMachineNames() const override { return {}; }
    bool playAnimation (const std::string&, bool) override { return false; }
    bool playStateMachine (const std::string&) override { return false; }
    void stop() override {}
    bool setNumberInput (const std::string&, float) override { return false; }
    bool setBooleanInput (const std::string&, bool) override { return false; }
    bool fireTrigger (const std::string&) override { return false; }
    bool advance (float) override { return false; }
    const std::vector<uint8_t>& pixelBuffer() const override { return emptyPixels; }
    int width() const override { return 0; }
    int height() const override { return 0; }
    std::size_t stride() const override { return 0; }
    bool hasFrame() const override { return false; }

private:
    std::vector<uint8_t> emptyPixels;
};

#endif

RiveOffscreenRenderer::RiveOffscreenRenderer (Options options)
{
#if YUP_WINDOWS && YUP_RIVE_USE_D3D
    impl = std::make_unique<RiveOffscreenRendererD3D> (options);
#else
    impl = std::make_unique<RiveOffscreenRendererStub> (options);
#endif
}

RiveOffscreenRenderer::~RiveOffscreenRenderer() = default;

RiveOffscreenRenderer::RiveOffscreenRenderer (RiveOffscreenRenderer&&) noexcept = default;
RiveOffscreenRenderer& RiveOffscreenRenderer::operator= (RiveOffscreenRenderer&&) noexcept = default;

void RiveOffscreenRenderer::loadFromFile (const std::string& path, std::optional<std::string_view> artboard)
{
    loadFromData (readFileToMemory (path), artboard);
}

void RiveOffscreenRenderer::loadFromData (const std::vector<uint8_t>& data, std::optional<std::string_view> artboard)
{
    impl->load (data, artboard);
}

std::vector<std::string> RiveOffscreenRenderer::animationNames() const
{
    return impl->animationNames();
}

std::vector<std::string> RiveOffscreenRenderer::stateMachineNames() const
{
    return impl->stateMachineNames();
}

bool RiveOffscreenRenderer::playAnimation (const std::string& name, bool loop)
{
    return impl->playAnimation (name, loop);
}

bool RiveOffscreenRenderer::playStateMachine (const std::string& name)
{
    return impl->playStateMachine (name);
}

void RiveOffscreenRenderer::stop()
{
    impl->stop();
}

bool RiveOffscreenRenderer::setNumberInput (const std::string& name, float value)
{
    return impl->setNumberInput (name, value);
}

bool RiveOffscreenRenderer::setBooleanInput (const std::string& name, bool value)
{
    return impl->setBooleanInput (name, value);
}

bool RiveOffscreenRenderer::fireTrigger (const std::string& name)
{
    return impl->fireTrigger (name);
}

bool RiveOffscreenRenderer::advance (float deltaSeconds)
{
    return impl->advance (deltaSeconds);
}

const std::vector<uint8_t>& RiveOffscreenRenderer::pixelBuffer() const
{
    return impl->pixelBuffer();
}

int RiveOffscreenRenderer::width() const
{
    return impl->width();
}

int RiveOffscreenRenderer::height() const
{
    return impl->height();
}

std::size_t RiveOffscreenRenderer::stride() const
{
    return impl->stride();
}

bool RiveOffscreenRenderer::hasFrame() const
{
    return impl->hasFrame();
}

} // namespace yup

