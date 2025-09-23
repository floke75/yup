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
    name:               YUP Rive Renderer Helpers
    description:        Helpers for driving Rive animations and preparing frames for streaming.
    website:            https://github.com/kunitoki/yup
    license:            ISC

    dependencies:       yup_core yup_graphics
    searchpaths:        native

  END_YUP_MODULE_DECLARATION

  ==============================================================================
*/

#pragma once
#define YUP_RIVE_RENDERER_H_INCLUDED

#include <yup_core/yup_core.h>
#include <yup_graphics/yup_graphics.h>

#include <rive/artboard.hpp>
#include <rive/animation/linear_animation_instance.hpp>
#include <rive/animation/state_machine_instance.hpp>
#include <rive/file.hpp>

#include "engine/yup_RiveAnimationEngine.h"
#include "renderer/yup_RiveOffscreenRenderer.h"
#include "renderer/yup_RiveRenderer.h"
