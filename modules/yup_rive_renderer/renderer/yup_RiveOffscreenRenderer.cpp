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

#include <rive/artboard.hpp>
#include <rive/renderer.hpp>

namespace yup::rive_renderer
{

namespace
{
GraphicsContext::Options makeContextOptions (const RiveOffscreenRenderer::Options& rendererOptions)
{
    GraphicsContext::Options opts;
    opts.allowHeadlessRendering = true;
    opts.enableReadPixels = rendererOptions.enableReadableFramebuffer;
    opts.readableFramebuffer = rendererOptions.enableReadableFramebuffer;
    opts.retinaDisplay = false;
    return opts;
}
}

RiveOffscreenRenderer::RiveOffscreenRenderer (Options options)
    : options (options)
{
    resize (options.width, options.height);
    ensureContext();
}

RiveOffscreenRenderer::~RiveOffscreenRenderer() = default;

RiveOffscreenRenderer::RiveOffscreenRenderer (RiveOffscreenRenderer&& other) noexcept = default;
RiveOffscreenRenderer& RiveOffscreenRenderer::operator= (RiveOffscreenRenderer&& other) noexcept = default;

rive::Factory* RiveOffscreenRenderer::factory() const noexcept
{
    return graphicsContext != nullptr ? graphicsContext->factory() : nullptr;
}

void RiveOffscreenRenderer::resize (uint32_t newWidth, uint32_t newHeight)
{
    renderWidth = newWidth;
    renderHeight = newHeight;
    allocateCpuBuffer();

    if (graphicsContext != nullptr)
        riveRenderer = graphicsContext->makeRenderer (static_cast<int> (renderWidth), static_cast<int> (renderHeight));
}

void RiveOffscreenRenderer::clear()
{
    std::fill (cpuBuffer.begin(), cpuBuffer.end(), static_cast<uint8> (0));
}

Span<const uint8> RiveOffscreenRenderer::pixelData() const noexcept
{
    return { cpuBuffer.data(), cpuBuffer.size() };
}

Span<uint8> RiveOffscreenRenderer::mutablePixelData() noexcept
{
    return { cpuBuffer.data(), cpuBuffer.size() };
}

bool RiveOffscreenRenderer::render (rive::ArtboardInstance& artboard)
{
    if (! ensureContext() || riveRenderer == nullptr)
        return false;

    artboard.draw (riveRenderer.get());
    return false;
}

void RiveOffscreenRenderer::allocateCpuBuffer()
{
    if (renderWidth == 0 || renderHeight == 0)
    {
        cpuBuffer.clear();
        return;
    }

    const auto size = static_cast<size_t> (renderWidth) * static_cast<size_t> (renderHeight) * bytesPerPixel();
    cpuBuffer.resize (size);
    clear();
}

bool RiveOffscreenRenderer::ensureContext()
{
    if (graphicsContext != nullptr)
        return true;

    auto ctxOptions = makeContextOptions (options);

#if YUP_WINDOWS
    graphicsContext = GraphicsContext::createContext (GraphicsContext::Direct3D, ctxOptions);
#endif

    if (graphicsContext == nullptr)
        graphicsContext = GraphicsContext::createContext (GraphicsContext::Headless, ctxOptions);

    if (graphicsContext != nullptr)
        riveRenderer = graphicsContext->makeRenderer (static_cast<int> (renderWidth), static_cast<int> (renderHeight));

    return graphicsContext != nullptr;
}

} // namespace yup::rive_renderer
