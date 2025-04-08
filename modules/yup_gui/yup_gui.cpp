/*
  ==============================================================================

   This file is part of the YUP library.
   Copyright (c) 2024 - kunitoki@gmail.com

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

#ifdef YUP_GUI_H_INCLUDED
/* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
 */
#error "Incorrect use of YUP cpp file"
#endif

#include "yup_gui.h"

//==============================================================================
#include <rive/layout.hpp>
#include <rive/custom_property_number.hpp>
#include <rive/custom_property_boolean.hpp>
#include <rive/custom_property_string.hpp>
#include <rive/animation/state_machine_instance.hpp>
#include <rive/animation/state_machine_input_instance.hpp>

//==============================================================================
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <SDL2/SDL_main.h>

//==============================================================================
#if JUCE_MAC || JUCE_IOS
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#if JUCE_MAC
#import <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>

#include "native/yup_Windowing_mac.mm"
#endif

#if JUCE_IOS
#include "native/yup_Windowing_ios.mm"
#endif

//==============================================================================
#elif JUCE_LINUX
#include <X11/Xlib.h>
#include <dlfcn.h>

#undef None
#undef KeyPress
#undef SIZEOF

#include "native/yup_Windowing_linux.cpp"

//==============================================================================
#elif JUCE_WINDOWS
#include "native/yup_Windowing_windows.cpp"

//==============================================================================
#elif JUCE_ANDROID

//==============================================================================
#elif JUCE_EMSCRIPTEN
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

//==============================================================================
#else

#endif

//==============================================================================
#include "application/yup_Application.cpp"
#include "desktop/yup_Desktop.cpp"
#include "mouse/yup_MouseEvent.cpp"
#include "mouse/yup_MouseCursor.cpp"
#include "component/yup_ComponentNative.cpp"
#include "component/yup_Component.cpp"
#include "widgets/yup_Button.cpp"
#include "widgets/yup_TextButton.cpp"
#include "widgets/yup_Slider.cpp"
#include "artboard/yup_Artboard.cpp"
#include "windowing/yup_DocumentWindow.cpp"

//==============================================================================
#include "native/yup_WindowingUtilities_sdl2.cpp"
#include "native/yup_Windowing_sdl2.h"
#include "native/yup_Windowing_sdl2.cpp"
