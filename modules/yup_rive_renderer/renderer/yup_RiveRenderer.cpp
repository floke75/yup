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

#include "yup_RiveRenderer.h"

#include <algorithm>

#include <yup_core/file/yup_File.h>
#include <yup_core/memory/yup_MemoryBlock.h>

namespace yup::rive_renderer
{

RiveRenderer::RiveRenderer (CreationOptions options)
    : creationOptions (options)
    , renderer (options.rendererOptions)
{
}

bool RiveRenderer::load (const File& file)
{
    if (! ensureRendererIsConfigured())
        return false;

    if (! animationEngine.loadFromFile (file, *renderer.factory(), creationOptions.loadOptions))
        return false;

    if (creationOptions.rendererOptions.width == 0 || creationOptions.rendererOptions.height == 0)
    {
        const auto [artboardWidth, artboardHeight] = animationEngine.artboardDimensions();
        const auto width = static_cast<uint32_t> (std::max (1.0f, artboardWidth));
        const auto height = static_cast<uint32_t> (std::max (1.0f, artboardHeight));
        renderer.resize (width, height);
    }

    return true;
}

bool RiveRenderer::loadFromData (Span<const uint8> data)
{
    if (! ensureRendererIsConfigured())
        return false;

    if (! animationEngine.loadFromData (data, *renderer.factory(), creationOptions.loadOptions))
        return false;

    if (creationOptions.rendererOptions.width == 0 || creationOptions.rendererOptions.height == 0)
    {
        const auto [artboardWidth, artboardHeight] = animationEngine.artboardDimensions();
        renderer.resize (static_cast<uint32_t> (std::max (1.0f, artboardWidth)),
                         static_cast<uint32_t> (std::max (1.0f, artboardHeight)));
    }

    return true;
}

bool RiveRenderer::advance (float deltaSeconds)
{
    const auto didAdvance = animationEngine.advance (deltaSeconds);

    if (didAdvance)
        renderFrame();

    return didAdvance;
}

bool RiveRenderer::renderFrame()
{
    auto* artboard = animationEngine.artboard();
    if (artboard == nullptr)
        return false;

    return renderer.render (*artboard);
}

bool RiveRenderer::ensureRendererIsConfigured()
{
    if (renderer.factory() != nullptr)
        return true;

    return renderer.isInitialised();
}

} // namespace yup::rive_renderer
