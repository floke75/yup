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
#include <string>
#include <vector>

#include <yup_core/yup_core.h>

namespace rive
{
class ArtboardInstance;
class Factory;
class LinearAnimationInstance;
class StateMachineInstance;
}

namespace yup
{
class File;
}

namespace yup::rive_renderer
{

/** Manages the lifecycle of a Rive file, including loading, animation control,
    state machine interaction and frame advancement. */
class YUP_API RiveAnimationEngine
{
public:
    struct LoadOptions
    {
        std::string artboardName; ///< Optional artboard name to load.
    };

    RiveAnimationEngine() = default;
    ~RiveAnimationEngine();

    RiveAnimationEngine (RiveAnimationEngine&&) noexcept = default;
    RiveAnimationEngine& operator= (RiveAnimationEngine&&) noexcept = default;

    RiveAnimationEngine (const RiveAnimationEngine&) = delete;
    RiveAnimationEngine& operator= (const RiveAnimationEngine&) = delete;

    /** Loads a Rive file from disk using the provided factory. */
    bool loadFromFile (const File& file,
                       rive::Factory& factory,
                       const LoadOptions& options = {});

    /** Loads a Rive file from a memory buffer. */
    bool loadFromData (Span<const uint8> data,
                       rive::Factory& factory,
                       const LoadOptions& options = {});

    /** Returns true when a file and artboard have been loaded successfully. */
    bool isLoaded() const noexcept;

    /** Returns the active artboard instance. */
    rive::ArtboardInstance* artboard() noexcept { return artboardInstance.get(); }
    const rive::ArtboardInstance* artboard() const noexcept { return artboardInstance.get(); }

    /** Returns a snapshot of animation names available on the artboard. */
    std::vector<std::string> animationNames() const;

    /** Returns a snapshot of state machine names available on the artboard. */
    std::vector<std::string> stateMachineNames() const;

    /** Starts playing a linear animation by name. */
    bool playAnimation (const std::string& name, bool loop);

    /** Starts a state machine by name. */
    bool playStateMachine (const std::string& name);

    /** Stops any running animation or state machine. */
    void stop();

    /** Pauses the currently active animation or state machine. */
    void setPaused (bool shouldPause) noexcept { isPaused = shouldPause; }
    bool paused() const noexcept { return isPaused; }

    /** Advances the animation/state-machine by the provided delta time. */
    bool advance (float deltaSeconds);

    /** Sets the value of a numerical state machine input. */
    bool setNumberInput (const std::string& name, float value);

    /** Sets the value of a boolean state machine input. */
    bool setBooleanInput (const std::string& name, bool value);

    /** Fires a trigger input on the active state machine. */
    bool fireTrigger (const std::string& name);

    /** Returns the width/height from the artboard layout if available. */
    std::pair<float, float> artboardDimensions() const noexcept;

    /** Returns the name of the currently playing animation, if any. */
    const std::optional<std::string>& currentAnimation() const noexcept { return activeAnimationName; }

    /** Returns the name of the currently active state machine, if any. */
    const std::optional<std::string>& currentStateMachine() const noexcept { return activeStateMachineName; }

private:
    bool selectArtboard (const LoadOptions& options);
    void resetPlaybackState();

    std::unique_ptr<rive::File> riveFile;
    std::unique_ptr<rive::ArtboardInstance> artboardInstance;
    std::unique_ptr<rive::LinearAnimationInstance> animationInstance;
    std::unique_ptr<rive::StateMachineInstance> stateMachineInstance;
    std::optional<std::string> activeAnimationName;
    std::optional<std::string> activeStateMachineName;
    bool loopAnimation = true;
    bool isPaused = false;
};

} // namespace yup::rive_renderer
