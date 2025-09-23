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

/*
  ==============================================================================

  BEGIN_YUP_MODULE_DECLARATION

    ID:                 yup_rive_renderer
    vendor:             yup
    version:            0.1.0
    name:               YUP Rive Offscreen Renderer
    description:        Infrastructure for offscreen Rive playback and frame management.
    website:            https://github.com/kunitoki/yup
    license:            ISC

    dependencies:       yup_core rive

  END_YUP_MODULE_DECLARATION

  ==============================================================================
*/

#pragma once
#define YUP_RIVE_RENDERER_H_INCLUDED

#include <yup_core/yup_core.h>

#include <rive/animation/linear_animation_instance.hpp>
#include <rive/animation/state_machine_instance.hpp>
#include <rive/file.hpp>
#include <rive/utils/no_op_factory.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace yup::rive_offscreen
{

struct FrameBufferView
{
    const std::uint8_t* data = nullptr;
    std::size_t sizeInBytes = 0;
    std::size_t rowStrideBytes = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] bool isValid() const noexcept { return data != nullptr && sizeInBytes != 0; }
};

struct LoadOptions
{
    yup::String artboardName;
    std::optional<std::uint32_t> widthOverride;
    std::optional<std::uint32_t> heightOverride;
};

struct LoadResult
{
    bool success = false;
    yup::String message;

    [[nodiscard]] static LoadResult ok()
    {
        return { true, {} };
    }

    [[nodiscard]] static LoadResult fail (yup::String reason)
    {
        return { false, std::move (reason) };
    }

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

enum class StateMachineInputType
{
    boolean,
    number,
    trigger
};

struct StateMachineInputInfo
{
    yup::String name;
    StateMachineInputType type = StateMachineInputType::boolean;
};

class RiveAnimationEngine
{
public:
    RiveAnimationEngine();
    ~RiveAnimationEngine();

    [[nodiscard]] bool isLoaded() const noexcept;

    LoadResult loadFromFile (const yup::File& file, const LoadOptions& options = {});
    void unload();

    [[nodiscard]] std::vector<yup::String> artboardNames() const;
    [[nodiscard]] std::vector<yup::String> animationNames() const;
    [[nodiscard]] std::vector<yup::String> stateMachineNames() const;

    bool playAnimation (const yup::String& name, bool loop = true);
    bool playStateMachine (const yup::String& name);
    void stop();

    void pause();
    void resume();
    [[nodiscard]] bool isPaused() const noexcept;

    bool setStateMachineBoolean (const yup::String& name, bool value);
    bool setStateMachineNumber (const yup::String& name, float value);
    bool fireStateMachineTrigger (const yup::String& name);
    [[nodiscard]] std::vector<StateMachineInputInfo> stateMachineInputs() const;

    bool advance (float deltaSeconds);

    [[nodiscard]] FrameBufferView frameBuffer() const noexcept;
    [[nodiscard]] std::uint32_t frameWidth() const noexcept;
    [[nodiscard]] std::uint32_t frameHeight() const noexcept;
    [[nodiscard]] std::size_t frameRowStride() const noexcept;
    [[nodiscard]] std::uint64_t frameCounter() const noexcept;

private:
    void clearPlayback();
    void rebuildInputCache();
    void ensureFrameStorage();
    bool advanceAnimation (float deltaSeconds);
    bool advanceStateMachine (float deltaSeconds);
    void touchFrameBuffer();

    rive::NoOpFactory factory;
    std::unique_ptr<rive::File> riveFile;
    std::unique_ptr<rive::ArtboardInstance> artboard;

    std::unique_ptr<rive::LinearAnimationInstance> activeAnimation;
    std::unique_ptr<rive::StateMachineInstance> activeStateMachine;

    std::unordered_map<std::string, rive::SMIBool*> boolInputs;
    std::unordered_map<std::string, rive::SMINumber*> numberInputs;
    std::unordered_map<std::string, rive::SMITrigger*> triggerInputs;
    std::vector<StateMachineInputInfo> cachedInputs;

    bool paused = false;
    bool loopAnimation = true;

    std::vector<std::uint8_t> frameData;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t rowStride = 0;
    std::uint64_t frameId = 0;
};

} // namespace yup::rive_offscreen

