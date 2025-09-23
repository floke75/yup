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

#include <optional>
#include <vector>

#include <yup_core/yup_core.h>

#include "yup_RiveOffscreenRenderer.h"
#include "../engine/yup_RiveAnimationEngine.h"

namespace yup::rive_renderer
{

/** High level convenience wrapper combining the animation engine and the
    offscreen renderer. */
class YUP_API RiveRenderer
{
public:
    struct CreationOptions
    {
        RiveOffscreenRenderer::Options rendererOptions;
        RiveAnimationEngine::LoadOptions loadOptions;
    };

    explicit RiveRenderer (CreationOptions options = {});

    bool load (const File& file);
    bool loadFromData (Span<const uint8> data);

    std::vector<std::string> animationNames() const { return animationEngine.animationNames(); }
    std::vector<std::string> stateMachineNames() const { return animationEngine.stateMachineNames(); }

    bool playAnimation (const std::string& name, bool loop = true) { return animationEngine.playAnimation (name, loop); }
    bool playStateMachine (const std::string& name) { return animationEngine.playStateMachine (name); }

    void stop() { animationEngine.stop(); }
    void pause() { animationEngine.setPaused (true); }
    void resume() { animationEngine.setPaused (false); }
    bool paused() const noexcept { return animationEngine.paused(); }

    bool setNumberInput (const std::string& name, float value) { return animationEngine.setNumberInput (name, value); }
    bool setBooleanInput (const std::string& name, bool value) { return animationEngine.setBooleanInput (name, value); }
    bool fireTrigger (const std::string& name) { return animationEngine.fireTrigger (name); }

    bool advance (float deltaSeconds);

    uint32_t width() const noexcept { return renderer.width(); }
    uint32_t height() const noexcept { return renderer.height(); }

    Span<const uint8> pixelData() const noexcept { return renderer.pixelData(); }

    bool renderFrame();

    const std::optional<std::string>& currentAnimation() const noexcept { return animationEngine.currentAnimation(); }
    const std::optional<std::string>& currentStateMachine() const noexcept { return animationEngine.currentStateMachine(); }

private:
    bool ensureRendererIsConfigured();

    CreationOptions creationOptions;
    RiveOffscreenRenderer renderer;
    RiveAnimationEngine animationEngine;
};

} // namespace yup::rive_renderer
