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

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <yup_core/yup_core.h>
#include <yup_graphics/context/yup_GraphicsContext.h>

namespace rive
{
class ArtboardInstance;
class Factory;
}

namespace yup::rive_renderer
{

/** Offscreen renderer responsible for preparing frame buffers suitable for
    downstream processing.

    The current implementation focuses on providing an allocation and life-cycle
    for the backing buffers while deferring platform specific GPU composition to
    future iterations. */
class YUP_API RiveOffscreenRenderer
{
public:
    struct Options
    {
        uint32_t width = 0;
        uint32_t height = 0;
        bool enableReadableFramebuffer = true;
    };

    explicit RiveOffscreenRenderer (Options options = {});
    ~RiveOffscreenRenderer();

    RiveOffscreenRenderer (const RiveOffscreenRenderer&) = delete;
    RiveOffscreenRenderer& operator= (const RiveOffscreenRenderer&) = delete;

    RiveOffscreenRenderer (RiveOffscreenRenderer&&) noexcept;
    RiveOffscreenRenderer& operator= (RiveOffscreenRenderer&&) noexcept;

    /** Returns the factory used for importing Rive files. */
    rive::Factory* factory() const noexcept;

    /** Resizes the output buffer, invalidating previously captured data. */
    void resize (uint32_t newWidth, uint32_t newHeight);

    /** Clears the CPU side buffer. */
    void clear();

    /** Returns the width of the current render target. */
    uint32_t width() const noexcept { return renderWidth; }

    /** Returns the height of the current render target. */
    uint32_t height() const noexcept { return renderHeight; }

    /** Returns the bytes per pixel for the output format (BGRA8). */
    static constexpr uint32_t bytesPerPixel() noexcept { return 4; }

    /** Returns a read-only view over the CPU buffer. */
    Span<const uint8> pixelData() const noexcept;

    /** Provides mutable access to the CPU buffer. */
    Span<uint8> mutablePixelData() noexcept;

    /** Attempts to render the provided artboard into the offscreen buffer. */
    bool render (rive::ArtboardInstance& artboard);

    /** Returns true if the renderer owns a valid graphics context. */
    bool isInitialised() const noexcept { return graphicsContext != nullptr; }

private:
    void allocateCpuBuffer();
    bool ensureContext();

    Options options;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;

    std::unique_ptr<GraphicsContext> graphicsContext;
    std::unique_ptr<rive::Renderer> riveRenderer;
    std::vector<uint8> cpuBuffer;
};

} // namespace yup::rive_renderer
