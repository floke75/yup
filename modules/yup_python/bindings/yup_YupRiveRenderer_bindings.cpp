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

#include "yup_YupRiveRenderer_bindings.h"

#if YUP_MODULE_AVAILABLE_yup_rive_renderer

#include "../utilities/yup_PyBind11Includes.h"
#include "../utilities/yup_PythonInterop.h"

#include <yup_rive_renderer/yup_rive_renderer.h>

#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <optional>
#include <cstring>
#include <stdexcept>

namespace yup::Bindings
{
namespace py = pybind11;
using namespace py::literals;

namespace
{
[[nodiscard]] static const char* inputTypeToString (StateMachineInputType type) noexcept
{
    switch (type)
    {
        case StateMachineInputType::boolean:
            return "boolean";
        case StateMachineInputType::number:
            return "number";
        case StateMachineInputType::trigger:
        default:
            return "trigger";
    }
}

[[nodiscard]] static py::array frameToArray (const RiveAnimationEngine& engine)
{
    const auto view = engine.frameBuffer();

    if (! view.isValid())
        throw std::runtime_error ("No frame data available. Did you load a file?");

    const auto width = static_cast<py::ssize_t> (view.width);
    const auto height = static_cast<py::ssize_t> (view.height);
    const auto channels = static_cast<py::ssize_t> (4);

    auto array = py::array_t<std::uint8_t> ({ height, width, channels });
    auto* dest = array.mutable_data();

    const auto rowBytes = static_cast<std::size_t> (view.width) * 4u;
    for (std::uint32_t y = 0; y < view.height; ++y)
    {
        const auto* src = view.data + (static_cast<std::size_t> (y) * view.rowStrideBytes);
        std::memcpy (dest + static_cast<std::size_t> (y) * rowBytes, src, rowBytes);
    }

    return array;
}

[[nodiscard]] static py::tuple makeLoadResultTuple (const LoadResult& result)
{
    return py::make_tuple (result.success, result.message.toStdString());
}

[[nodiscard]] static std::vector<py::object> convertStringList (const std::vector<yup::String>& values)
{
    std::vector<py::object> output;
    output.reserve (values.size());

    for (const auto& value : values)
        output.emplace_back (py::str (value.toStdString()));

    return output;
}

} // namespace

void registerYupRiveRendererBindings (py::module_& module)
{
    auto riveModule = module.def_submodule ("rive");

    py::class_<RiveAnimationEngine> (riveModule, "AnimationEngine")
        .def (py::init<>())
        .def ("load_file",
              [] (RiveAnimationEngine& engine,
                  const std::string& path,
                  std::optional<std::string> artboard,
                  std::optional<std::uint32_t> width,
                  std::optional<std::uint32_t> height)
              {
                  yup::File file { yup::String (path) };

                  LoadOptions options;
                  if (artboard.has_value())
                      options.artboardName = yup::String (*artboard);
                  if (width.has_value())
                      options.widthOverride = width.value();
                  if (height.has_value())
                      options.heightOverride = height.value();

                  return makeLoadResultTuple (engine.loadFromFile (file, options));
              },
              "path"_a,
              "artboard"_a = std::nullopt,
              "width"_a = std::nullopt,
              "height"_a = std::nullopt)
        .def ("unload", &RiveAnimationEngine::unload)
        .def ("is_loaded", &RiveAnimationEngine::isLoaded)
        .def ("artboard_names",
              [] (const RiveAnimationEngine& engine)
              {
                  return convertStringList (engine.artboardNames());
              })
        .def ("animation_names",
              [] (const RiveAnimationEngine& engine)
              {
                  return convertStringList (engine.animationNames());
              })
        .def ("state_machine_names",
              [] (const RiveAnimationEngine& engine)
              {
                  return convertStringList (engine.stateMachineNames());
              })
        .def ("play_animation", &RiveAnimationEngine::playAnimation, "name"_a, "loop"_a = true)
        .def ("play_state_machine", &RiveAnimationEngine::playStateMachine, "name"_a)
        .def ("stop", &RiveAnimationEngine::stop)
        .def ("pause", &RiveAnimationEngine::pause)
        .def ("resume", &RiveAnimationEngine::resume)
        .def ("is_paused", &RiveAnimationEngine::isPaused)
        .def ("set_state_boolean", &RiveAnimationEngine::setStateMachineBoolean, "name"_a, "value"_a)
        .def ("set_state_number", &RiveAnimationEngine::setStateMachineNumber, "name"_a, "value"_a)
        .def ("fire_state_trigger", &RiveAnimationEngine::fireStateMachineTrigger, "name"_a)
        .def ("state_machine_inputs",
              [] (const RiveAnimationEngine& engine)
              {
                  py::list result;
                  for (const auto& input : engine.stateMachineInputs())
                  {
                      py::dict item;
                      item["name"] = py::str (input.name.toStdString());
                      item["type"] = py::str (inputTypeToString (input.type));
                      result.append (std::move (item));
                  }
                  return result;
              })
        .def ("advance",
              [] (RiveAnimationEngine& engine, float deltaSeconds)
              {
                  py::gil_scoped_release release;
                  return engine.advance (deltaSeconds);
              },
              "delta_seconds"_a)
        .def ("frame_width", &RiveAnimationEngine::frameWidth)
        .def ("frame_height", &RiveAnimationEngine::frameHeight)
        .def ("frame_stride", &RiveAnimationEngine::frameRowStride)
        .def ("frame_counter", &RiveAnimationEngine::frameCounter)
        .def ("frame_data",
              [] (const RiveAnimationEngine& engine)
              {
                  return frameToArray (engine);
              });
}

} // namespace yup::Bindings

#endif // YUP_MODULE_AVAILABLE_yup_rive_renderer
