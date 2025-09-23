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

#include "yup_RiveAnimationEngine.h"

#include <rive/artboard.hpp>
#include <rive/animation/linear_animation_instance.hpp>
#include <rive/animation/loop.hpp>
#include <rive/animation/state_machine_instance.hpp>
#include <rive/animation/state_machine_input_instance.hpp>
#include <rive/file.hpp>

#include <yup_core/file/yup_File.h>
#include <yup_core/memory/yup_MemoryBlock.h>

namespace yup::rive_renderer
{

namespace
{
std::vector<std::string> collectStateMachineNames (const rive::ArtboardInstance& artboard)
{
    std::vector<std::string> names;
    const auto count = artboard.stateMachineCount();
    names.reserve (count);

    for (size_t i = 0; i < count; ++i)
    {
        names.push_back (artboard.stateMachineNameAt (i));
    }

    return names;
}

} // namespace

RiveAnimationEngine::~RiveAnimationEngine() = default;

bool RiveAnimationEngine::loadFromFile (const File& file,
                                        rive::Factory& factory,
                                        const LoadOptions& options)
{
    if (! file.existsAsFile())
        return false;

    auto input = file.createInputStream();
    if (input == nullptr || ! input->openedOk())
        return false;

    MemoryBlock block;
    input->readIntoMemoryBlock (block);
    return loadFromData ({ static_cast<const uint8*> (block.getData()), block.getSize() }, factory, options);
}

bool RiveAnimationEngine::loadFromData (Span<const uint8> data,
                                        rive::Factory& factory,
                                        const LoadOptions& options)
{
    rive::ImportResult importResult = rive::ImportResult::malformed;
    auto file = rive::File::import ({ data.data(), data.size() }, &factory, &importResult);

    if (importResult != rive::ImportResult::success || file == nullptr)
        return false;

    riveFile = std::move (file);
    if (! selectArtboard (options))
    {
        riveFile.reset();
        return false;
    }

    resetPlaybackState();
    return true;
}

bool RiveAnimationEngine::isLoaded() const noexcept
{
    return riveFile != nullptr && artboardInstance != nullptr;
}

std::vector<std::string> RiveAnimationEngine::animationNames() const
{
    if (! isLoaded())
        return {};

    std::vector<std::string> names;
    const auto count = artboardInstance->animationCount();
    names.reserve (count);

    for (size_t i = 0; i < count; ++i)
        names.push_back (artboardInstance->animationNameAt (i));

    return names;
}

std::vector<std::string> RiveAnimationEngine::stateMachineNames() const
{
    if (! isLoaded())
        return {};

    return collectStateMachineNames (*artboardInstance);
}

bool RiveAnimationEngine::playAnimation (const std::string& name, bool loop)
{
    if (! isLoaded())
        return false;

    auto instance = artboardInstance->animationNamed (name);
    if (instance == nullptr)
        return false;

    animationInstance = std::move (instance);
    loopAnimation = loop;
    activeAnimationName = name;
    activeStateMachineName.reset();
    stateMachineInstance.reset();

    animationInstance->loopValue (loop ? static_cast<int> (rive::Loop::loop)
                                       : static_cast<int> (rive::Loop::oneShot));
    animationInstance->reset (1.0f);

    return true;
}

bool RiveAnimationEngine::playStateMachine (const std::string& name)
{
    if (! isLoaded())
        return false;

    auto instance = artboardInstance->stateMachineNamed (name);
    if (instance == nullptr)
        return false;

    stateMachineInstance = std::move (instance);
    activeStateMachineName = name;
    activeAnimationName.reset();
    animationInstance.reset();

    return true;
}

void RiveAnimationEngine::stop()
{
    animationInstance.reset();
    stateMachineInstance.reset();
    activeAnimationName.reset();
    activeStateMachineName.reset();
}

bool RiveAnimationEngine::advance (float deltaSeconds)
{
    if (! isLoaded() || isPaused)
        return false;

    bool didAdvance = false;

    if (animationInstance != nullptr)
    {
        const auto keepGoing = animationInstance->advanceAndApply (deltaSeconds);
        didAdvance = true;

        if (! keepGoing && ! loopAnimation)
        {
            animationInstance.reset();
            activeAnimationName.reset();
        }
    }

    if (stateMachineInstance != nullptr)
    {
        didAdvance |= stateMachineInstance->advanceAndApply (deltaSeconds);
    }

    if (didAdvance)
    {
        artboardInstance->advance (deltaSeconds);
    }

    return didAdvance;
}

bool RiveAnimationEngine::setNumberInput (const std::string& name, float value)
{
    if (stateMachineInstance == nullptr)
        return false;

    if (auto* numberInput = stateMachineInstance->getNumber (name))
    {
        numberInput->value (value);
        return true;
    }

    return false;
}

bool RiveAnimationEngine::setBooleanInput (const std::string& name, bool value)
{
    if (stateMachineInstance == nullptr)
        return false;

    if (auto* boolInput = stateMachineInstance->getBool (name))
    {
        boolInput->value (value);
        return true;
    }

    if (value)
    {
        if (auto* trigger = stateMachineInstance->getTrigger (name))
        {
            trigger->fire();
            return true;
        }
    }

    return false;
}

bool RiveAnimationEngine::fireTrigger (const std::string& name)
{
    if (stateMachineInstance == nullptr)
        return false;

    if (auto* trigger = stateMachineInstance->getTrigger (name))
    {
        trigger->fire();
        return true;
    }

    return false;
}

std::pair<float, float> RiveAnimationEngine::artboardDimensions() const noexcept
{
    if (! isLoaded())
        return { 0.0f, 0.0f };

    const auto width = artboardInstance->layoutWidth();
    const auto height = artboardInstance->layoutHeight();
    return { width, height };
}

bool RiveAnimationEngine::selectArtboard (const LoadOptions& options)
{
    if (riveFile == nullptr)
        return false;

    std::unique_ptr<rive::ArtboardInstance> instance;

    if (! options.artboardName.empty())
        instance = riveFile->artboardNamed (options.artboardName);

    if (instance == nullptr)
        instance = riveFile->artboardDefault();

    if (instance == nullptr && riveFile->artboardCount() > 0)
        instance = riveFile->artboardAt (0);

    if (instance == nullptr)
        return false;

    artboardInstance = std::move (instance);
    artboardInstance->advance (0.0f);
    return true;
}

void RiveAnimationEngine::resetPlaybackState()
{
    animationInstance.reset();
    stateMachineInstance.reset();
    activeAnimationName.reset();
    activeStateMachineName.reset();
    loopAnimation = true;
    isPaused = false;
}

} // namespace yup::rive_renderer
