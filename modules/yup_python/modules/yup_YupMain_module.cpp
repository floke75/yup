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

#include "../utilities/yup_PyBind11Includes.h"
#include "../utilities/yup_CrashHandling.h"

#include "../bindings/yup_YupCore_bindings.h"

#if YUP_MODULE_AVAILABLE_yup_events
#include "../bindings/yup_YupEvents_bindings.h"
#endif

/*
#if YUP_MODULE_AVAILABLE_yup_data_model
#include "../bindings/yup_YupDataModel_bindings.h"
#endif
*/

#if YUP_MODULE_AVAILABLE_yup_graphics
#include "../bindings/yup_YupGraphics_bindings.h"
#endif

#if YUP_MODULE_AVAILABLE_yup_rive_renderer
#include "../bindings/yup_YupRiveRenderer_bindings.h"
#endif

#if YUP_MODULE_AVAILABLE_yup_gui
#include "../bindings/yup_YupGui_bindings.h"
#endif

/*
#if YUP_MODULE_AVAILABLE_yup_audio_basics
#include "../bindings/yup_YupAudioBasics_bindings.h"
#endif

#if YUP_MODULE_AVAILABLE_yup_audio_devices
#include "../bindings/yup_YupAudioDevices_bindings.h"
#endif

#if YUP_MODULE_AVAILABLE_yup_audio_processors
#include "../bindings/yup_YupAudioProcessors_bindings.h"
#endif
*/

//==============================================================================

#if YUP_PYTHON_EMBEDDED_INTERPRETER
PYBIND11_EMBEDDED_MODULE (YUP_PYTHON_MODULE_NAME, m)
#else
PYBIND11_MODULE (YUP_PYTHON_MODULE_NAME, m)
#endif
{
    namespace py = pybind11;

    // ---- When running from wheel
#if ! YUP_PYTHON_EMBEDDED_INTERPRETER
    yup::SystemStats::setApplicationCrashHandler (yup::Helpers::applicationCrashHandler);

    m.attr ("__embedded_interpreter__") = false;
#else
    m.attr ("__embedded_interpreter__") = true;
#endif

    // ---- Register bindings
    yup::Bindings::registerYupCoreBindings (m);

#if YUP_MODULE_AVAILABLE_yup_events
    yup::Bindings::registerYupEventsBindings (m);
#endif

    /*
#if YUP_MODULE_AVAILABLE_yup_data_model
    yup::Bindings::registerYupDataModelBindings (m);
#endif
    */

#if YUP_MODULE_AVAILABLE_yup_graphics
    yup::Bindings::registerYupGraphicsBindings (m);
#endif

#if YUP_MODULE_AVAILABLE_yup_rive_renderer
    yup::Bindings::registerYupRiveRendererBindings (m);
#endif

#if YUP_MODULE_AVAILABLE_yup_gui
    yup::Bindings::registerYupGuiBindings (m);
#endif

    /*
#if YUP_MODULE_AVAILABLE_yup_audio_basics
    yup::Bindings::registerYupAudioBasicsBindings (m);
#endif

#if YUP_MODULE_AVAILABLE_yup_audio_devices
    yup::Bindings::registerYupAudioDevicesBindings (m);
#endif

#if YUP_MODULE_AVAILABLE_yup_audio_processors
    yup::Bindings::registerYupAudioProcessorsBindings (m);
#endif
*/
}
