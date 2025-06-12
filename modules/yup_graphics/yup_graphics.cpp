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

// clang-format off
#ifdef YUP_GRAPHICS_H_INCLUDED
/* When you add this cpp file to your project, you mustn't include it in a file where you've
    already included any other headers - just put it inside a file on its own, possibly with your config
    flags preceding it, but don't include anything else. That also includes avoiding any automatic prefix
    header files that the compiler may be using.
*/
#error "Incorrect use of YUP cpp file"
#endif
// clang-format on

#include "yup_graphics.h"

//==============================================================================

#include <rive/text/font_hb.hpp>

//==============================================================================

#if YUP_WINDOWS

#if YUP_RIVE_USE_D3D
#include <array>
#include <dxgi1_2.h>

#include "native/yup_GraphicsContext_d3d.cpp"
#endif

#if YUP_RIVE_USE_OPENGL
#include "native/yup_GraphicsContext_gl.cpp"
#endif

//==============================================================================

#elif YUP_MAC || YUP_IOS

#if YUP_RIVE_USE_METAL
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#if YUP_MAC
#import <Cocoa/Cocoa.h>
#else
#import <UIKit/UIKit.h>
#endif

#include "native/yup_GraphicsContext_metal.cpp"
#endif

#if YUP_RIVE_USE_OPENGL
#include "native/yup_GraphicsContext_gl.cpp"
#endif

//==============================================================================

#elif YUP_LINUX || YUP_WASM || YUP_ANDROID

#if YUP_EMSCRIPTEN && RIVE_WEBGL
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#endif

#include "native/yup_GraphicsContext_gl.cpp"

#endif

//==============================================================================

#if YUP_RIVE_USE_DAWN
#include "native/yup_GraphicsContext_dawn.cpp"
#include "native/yup_GraphicsContext_dawn_helper.cpp"
#endif

//==============================================================================

#include "native/yup_GraphicsContext_headless.cpp"

//==============================================================================

#include "native/yup_GraphicsContext_impl.cpp"

//==============================================================================
#include "primitives/yup_Path.cpp"
#include "fonts/yup_Font.cpp"
#include "fonts/yup_StyledText.cpp"
#include "imaging/yup_Image.cpp"
#include "graphics/yup_Color.cpp"
#include "graphics/yup_Colors.cpp"
#include "graphics/yup_Graphics.cpp"
