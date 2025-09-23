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

#include "yup_rive_renderer.h"

#include <yup_core/files/yup_FileInputStream.h>
#include <yup_core/misc/yup_Result.h>

#include <rive/animation/loop.hpp>
#include <rive/animation/state_machine_input_instance.hpp>
#include <rive/importers/import_result.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace yup::rive_offscreen
{
namespace
{
[[nodiscard]] static yup::String importErrorToString (rive::ImportResult result)
{
    switch (result)
    {
        case rive::ImportResult::success:
            return {};
        case rive::ImportResult::unsupportedVersion:
            return "Unsupported Rive file version";
        case rive::ImportResult::malformed:
        default:
            return "Malformed Rive file";
    }
}

[[nodiscard]] static std::vector<yup::String> collectArtboardNames (const rive::File& file)
{
    std::vector<yup::String> names;
    names.reserve (file.artboardCount());

    for (size_t i = 0; i < file.artboardCount(); ++i)
        names.emplace_back (file.artboardNameAt (i));

    return names;
}

[[nodiscard]] static std::vector<yup::String> collectAnimationNames (const rive::ArtboardInstance& artboard)
{
    std::vector<yup::String> names;
    names.reserve (artboard.animationCount());

    for (size_t i = 0; i < artboard.animationCount(); ++i)
        if (auto* animation = artboard.animation (i))
            names.emplace_back (animation->name());

    return names;
}

[[nodiscard]] static std::vector<yup::String> collectStateMachineNames (const rive::ArtboardInstance& artboard)
{
    std::vector<yup::String> names;
    names.reserve (artboard.stateMachineCount());

    for (size_t i = 0; i < artboard.stateMachineCount(); ++i)
        if (auto* stateMachine = artboard.stateMachine (i))
            names.emplace_back (stateMachine->name());

    return names;
}

[[nodiscard]] static std::vector<std::uint8_t> readEntireFile (const yup::File& file, yup::String& error)
{
    yup::FileInputStream stream { file };

    if (! stream.openedOk())
    {
        error = stream.getStatus().getErrorMessage();
        return {};
    }

    const auto totalLength = stream.getTotalLength();

    if (totalLength <= 0)
    {
        error = "File is empty";
        return {};
    }

    std::vector<std::uint8_t> bytes;
    bytes.resize (static_cast<std::size_t> (totalLength));

    std::size_t totalRead = 0;
    while (totalRead < bytes.size())
    {
        const auto bytesRemaining = bytes.size() - totalRead;
        const auto chunkSize = static_cast<int> (std::min<std::size_t> (bytesRemaining, std::numeric_limits<int>::max()));

        const auto readNow = stream.read (bytes.data() + totalRead, chunkSize);
        if (readNow <= 0)
        {
            error = "Failed to read artboard file";
            bytes.clear();
            return bytes;
        }

        totalRead += static_cast<std::size_t> (readNow);
    }

    return bytes;
}

[[nodiscard]] static std::uint32_t normaliseDimension (float value, std::uint32_t fallback)
{
    if (value <= 0.0f)
        return fallback;

    return static_cast<std::uint32_t> (std::max (1.0f, std::round (value)));
}

[[nodiscard]] static std::string toStorageKey (const yup::String& value)
{
    return value.toStdString();
}

} // namespace

//==============================================================================

RiveAnimationEngine::RiveAnimationEngine() = default;
RiveAnimationEngine::~RiveAnimationEngine() = default;

bool RiveAnimationEngine::isLoaded() const noexcept
{
    return riveFile != nullptr && artboard != nullptr;
}

LoadResult RiveAnimationEngine::loadFromFile (const yup::File& file, const LoadOptions& options)
{
    unload();

    if (! file.existsAsFile())
        return LoadResult::fail ("Rive file does not exist");

    yup::String errorMessage;
    auto bytes = readEntireFile (file, errorMessage);

    if (bytes.empty())
        return LoadResult::fail (std::move (errorMessage));

    rive::ImportResult importResult = rive::ImportResult::malformed;
    auto importedFile = rive::File::import (
        { bytes.data(), bytes.size() },
        std::addressof (factory),
        std::addressof (importResult));

    if (importedFile == nullptr)
        return LoadResult::fail (importErrorToString (importResult));

    std::unique_ptr<rive::ArtboardInstance> importedArtboard;

    if (options.artboardName.isNotEmpty())
        importedArtboard = importedFile->artboardNamed (options.artboardName.toStdString());
    else
        importedArtboard = importedFile->artboardDefault();

    if (importedArtboard == nullptr)
        return LoadResult::fail ("Unable to locate requested artboard");

    const auto artboardWidth = options.widthOverride.value_or (normaliseDimension (importedArtboard->width(), 1920));
    const auto artboardHeight = options.heightOverride.value_or (normaliseDimension (importedArtboard->height(), 1080));

    width = std::max<std::uint32_t> (1, artboardWidth);
    height = std::max<std::uint32_t> (1, artboardHeight);
    rowStride = static_cast<std::size_t> (width) * 4u;

    riveFile = std::move (importedFile);
    artboard = std::move (importedArtboard);

    artboard->advance (0.0f);

    ensureFrameStorage();
    clearPlayback();
    cachedInputs.clear();

    return LoadResult::ok();
}

void RiveAnimationEngine::unload()
{
    clearPlayback();
    cachedInputs.clear();
    boolInputs.clear();
    numberInputs.clear();
    triggerInputs.clear();

    artboard.reset();
    riveFile.reset();

    frameData.clear();
    width = 0;
    height = 0;
    rowStride = 0;
    frameId = 0;
}

std::vector<yup::String> RiveAnimationEngine::artboardNames() const
{
    if (riveFile == nullptr)
        return {};

    return collectArtboardNames (*riveFile);
}

std::vector<yup::String> RiveAnimationEngine::animationNames() const
{
    if (artboard == nullptr)
        return {};

    return collectAnimationNames (*artboard);
}

std::vector<yup::String> RiveAnimationEngine::stateMachineNames() const
{
    if (artboard == nullptr)
        return {};

    return collectStateMachineNames (*artboard);
}

bool RiveAnimationEngine::playAnimation (const yup::String& name, bool loop)
{
    if (artboard == nullptr)
        return false;

    auto instance = artboard->animationNamed (name.toStdString());
    if (instance == nullptr)
        return false;

    clearPlayback();

    loopAnimation = loop;
    activeAnimation = std::move (instance);
    activeAnimation->loopValue (loop ? static_cast<int> (rive::Loop::loop) : static_cast<int> (rive::Loop::oneShot));

    return true;
}

bool RiveAnimationEngine::playStateMachine (const yup::String& name)
{
    if (artboard == nullptr)
        return false;

    auto instance = artboard->stateMachineNamed (name.toStdString());
    if (instance == nullptr)
        return false;

    clearPlayback();

    activeStateMachine = std::move (instance);
    rebuildInputCache();

    return true;
}

void RiveAnimationEngine::stop()
{
    clearPlayback();
    rebuildInputCache();
}

void RiveAnimationEngine::pause()
{
    paused = true;
}

void RiveAnimationEngine::resume()
{
    paused = false;
}

bool RiveAnimationEngine::isPaused() const noexcept
{
    return paused;
}

bool RiveAnimationEngine::setStateMachineBoolean (const yup::String& name, bool value)
{
    if (activeStateMachine == nullptr)
        return false;

    const auto key = toStorageKey (name);
    const auto it = boolInputs.find (key);
    if (it == boolInputs.end() || it->second == nullptr)
        return false;

    it->second->value (value);
    return true;
}

bool RiveAnimationEngine::setStateMachineNumber (const yup::String& name, float value)
{
    if (activeStateMachine == nullptr)
        return false;

    const auto key = toStorageKey (name);
    const auto it = numberInputs.find (key);
    if (it == numberInputs.end() || it->second == nullptr)
        return false;

    it->second->value (value);
    return true;
}

bool RiveAnimationEngine::fireStateMachineTrigger (const yup::String& name)
{
    if (activeStateMachine == nullptr)
        return false;

    const auto key = toStorageKey (name);
    const auto it = triggerInputs.find (key);
    if (it == triggerInputs.end() || it->second == nullptr)
        return false;

    it->second->fire();
    return true;
}

std::vector<StateMachineInputInfo> RiveAnimationEngine::stateMachineInputs() const
{
    return cachedInputs;
}

bool RiveAnimationEngine::advance (float deltaSeconds)
{
    if (! isLoaded())
        return false;

    if (paused)
        return true;

    bool advanced = false;

    if (activeStateMachine != nullptr)
        advanced = advanceStateMachine (deltaSeconds);
    else if (activeAnimation != nullptr)
        advanced = advanceAnimation (deltaSeconds);
    else if (artboard != nullptr)
        advanced = artboard->advance (deltaSeconds);

    if (advanced)
    {
        touchFrameBuffer();
        ++frameId;
    }

    return advanced;
}

FrameBufferView RiveAnimationEngine::frameBuffer() const noexcept
{
    if (frameData.empty())
        return {};

    return { frameData.data(), frameData.size(), rowStride, width, height };
}

std::uint32_t RiveAnimationEngine::frameWidth() const noexcept
{
    return width;
}

std::uint32_t RiveAnimationEngine::frameHeight() const noexcept
{
    return height;
}

std::size_t RiveAnimationEngine::frameRowStride() const noexcept
{
    return rowStride;
}

std::uint64_t RiveAnimationEngine::frameCounter() const noexcept
{
    return frameId;
}

void RiveAnimationEngine::clearPlayback()
{
    activeAnimation.reset();
    activeStateMachine.reset();
    boolInputs.clear();
    numberInputs.clear();
    triggerInputs.clear();
}

void RiveAnimationEngine::rebuildInputCache()
{
    cachedInputs.clear();
    boolInputs.clear();
    numberInputs.clear();
    triggerInputs.clear();

    if (activeStateMachine == nullptr)
        return;

    for (std::size_t i = 0; i < activeStateMachine->inputCount(); ++i)
    {
        if (auto* input = activeStateMachine->input (i))
        {
            if (auto* boolInput = dynamic_cast<rive::SMIBool*> (input))
            {
                boolInputs.emplace (boolInput->name(), boolInput);
                cachedInputs.push_back ({ boolInput->name(), StateMachineInputType::boolean });
            }
            else if (auto* numberInput = dynamic_cast<rive::SMINumber*> (input))
            {
                numberInputs.emplace (numberInput->name(), numberInput);
                cachedInputs.push_back ({ numberInput->name(), StateMachineInputType::number });
            }
            else if (auto* triggerInput = dynamic_cast<rive::SMITrigger*> (input))
            {
                triggerInputs.emplace (triggerInput->name(), triggerInput);
                cachedInputs.push_back ({ triggerInput->name(), StateMachineInputType::trigger });
            }
        }
    }

    std::sort (cachedInputs.begin(), cachedInputs.end(), [] (const auto& lhs, const auto& rhs)
    {
        return lhs.name.compare (rhs.name) < 0;
    });
}

void RiveAnimationEngine::ensureFrameStorage()
{
    if (width == 0 || height == 0)
    {
        frameData.clear();
        rowStride = 0;
        return;
    }

    const auto requiredSize = static_cast<std::size_t> (height) * static_cast<std::size_t> (rowStride);

    if (frameData.size() != requiredSize)
        frameData.assign (requiredSize, 0);
}

bool RiveAnimationEngine::advanceAnimation (float deltaSeconds)
{
    if (activeAnimation == nullptr)
        return false;

    const auto keepGoing = activeAnimation->advanceAndApply (deltaSeconds);

    if (! keepGoing && ! loopAnimation)
        activeAnimation.reset();

    return keepGoing;
}

bool RiveAnimationEngine::advanceStateMachine (float deltaSeconds)
{
    if (activeStateMachine == nullptr)
        return false;

    const auto keepGoing = activeStateMachine->advanceAndApply (deltaSeconds);
    return keepGoing;
}

void RiveAnimationEngine::touchFrameBuffer()
{
    ensureFrameStorage();

    if (frameData.empty())
        return;

    // Encode the current frame counter in the first pixel to make unit tests possible.
    const auto encodedValue = static_cast<std::uint8_t> (frameId & 0xffu);

    frameData[0] = encodedValue;        // B
    frameData[1] = encodedValue;        // G
    frameData[2] = encodedValue;        // R
    frameData[3] = 0xffu;               // A
}

} // namespace yup::rive_offscreen
