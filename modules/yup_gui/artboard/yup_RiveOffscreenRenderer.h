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

namespace yup
{

class ArtboardFile;

//==============================================================================
/**
    Offscreen renderer for Rive artboards using the Direct3D11 backend.

    The renderer manages an offscreen GPU context, loads .riv files into
    artboards, advances animations or state machines, and exposes the rendered
    BGRA frame to callers.

    On platforms where the Direct3D backend is not available the renderer will
    fail gracefully and report an informative error when attempting to load
    content or render frames.
*/
class YUP_API RiveOffscreenRenderer
{
public:
    /** Creates a renderer for the given output dimensions. */
    RiveOffscreenRenderer (int width, int height);

    /** Destructor. */
    ~RiveOffscreenRenderer();

    /** Returns true when the underlying GPU resources were initialised. */
    bool isValid() const noexcept;

    /** Loads a Rive file from disk. */
    Result load (const File& file, const String& artboardName = {});

    /** Lists the available linear animations in the currently loaded artboard. */
    StringArray listAnimations() const;

    /** Lists the available state machines in the currently loaded artboard. */
    StringArray listStateMachines() const;

    /** Starts playing the specified linear animation. */
    bool playAnimation (const String& animationName, bool shouldLoop = true);

    /** Starts playing the specified state machine. */
    bool playStateMachine (const String& machineName);

    /** Stops any running animation or state machine. */
    void stop();

    /** Pauses or resumes advancement of the current scene. */
    void setPaused (bool shouldPause);

    /** Returns true when the renderer is paused. */
    bool isPaused() const noexcept;

    /** Sets a boolean input on the active state machine. */
    bool setBoolInput (const String& name, bool value);

    /** Sets a numeric input on the active state machine. */
    bool setNumberInput (const String& name, double value);

    /** Fires a trigger input on the active state machine. */
    bool fireTriggerInput (const String& name);

    /** Advances the active scene and renders a new frame. */
    bool advance (float deltaSeconds);

    /** Returns the width of the offscreen surface. */
    int getWidth() const noexcept;

    /** Returns the height of the offscreen surface. */
    int getHeight() const noexcept;

    /** Returns the stride in bytes for each row in the frame buffer. */
    std::size_t getRowStride() const noexcept;

    /** Returns the most recent BGRA frame contents. */
    const std::vector<uint8>& getFrameBuffer() const noexcept;

    /** Returns a shared reference to the frame buffer for use with Python bindings. */
    std::shared_ptr<const std::vector<uint8>> getFrameBufferShared() const noexcept;

    /** Returns the last error that occurred while operating the renderer. */
    const String& getLastError() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace yup

