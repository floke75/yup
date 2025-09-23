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

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yup
{

class YUP_API RiveOffscreenRenderer
{
public:
    struct Options
    {
        int width = 0;
        int height = 0;
        bool disableRasterOrdering = false;
    };

    explicit RiveOffscreenRenderer (Options options = {});
    ~RiveOffscreenRenderer();

    RiveOffscreenRenderer (const RiveOffscreenRenderer&) = delete;
    RiveOffscreenRenderer& operator= (const RiveOffscreenRenderer&) = delete;
    RiveOffscreenRenderer (RiveOffscreenRenderer&&) noexcept;
    RiveOffscreenRenderer& operator= (RiveOffscreenRenderer&&) noexcept;

    void loadFromFile (const std::string& path, std::optional<std::string_view> artboard = std::nullopt);
    void loadFromData (const std::vector<uint8_t>& data, std::optional<std::string_view> artboard = std::nullopt);

    [[nodiscard]] std::vector<std::string> animationNames() const;
    [[nodiscard]] std::vector<std::string> stateMachineNames() const;

    bool playAnimation (const std::string& name, bool loop = true);
    bool playStateMachine (const std::string& name);
    void stop();

    bool setNumberInput (const std::string& name, float value);
    bool setBooleanInput (const std::string& name, bool value);
    bool fireTrigger (const std::string& name);

    bool advance (float deltaSeconds);

    [[nodiscard]] const std::vector<uint8_t>& pixelBuffer() const;
    [[nodiscard]] int width() const;
    [[nodiscard]] int height() const;
    [[nodiscard]] std::size_t stride() const;

    [[nodiscard]] bool hasFrame() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace yup

