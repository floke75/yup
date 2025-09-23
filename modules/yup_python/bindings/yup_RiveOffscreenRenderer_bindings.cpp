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

#include "yup_YupGraphics_bindings.h"

#define YUP_PYTHON_INCLUDE_PYBIND11_NUMPY
#define YUP_PYTHON_INCLUDE_PYBIND11_STL
#include "../utilities/yup_PyBind11Includes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yup::Bindings
{
namespace py = pybind11;

static py::array makeFrameArray (yup::RiveOffscreenRenderer& renderer)
{
    if (! renderer.hasFrame())
        throw py::value_error ("No frame has been rendered yet");

    const auto width = renderer.width();
    const auto height = renderer.height();
    if (width <= 0 || height <= 0)
        throw py::value_error ("Renderer has invalid dimensions");

    const auto& pixels = renderer.pixelBuffer();
    const std::size_t expected = static_cast<std::size_t> (width) * static_cast<std::size_t> (height) * 4u;
    if (pixels.size() < expected)
        throw py::value_error ("Frame buffer is smaller than expected");

    auto capsule = py::capsule (pixels.data(), [](void*) {});
    return py::array (py::buffer_info (const_cast<uint8_t*> (pixels.data()),
                                       sizeof (uint8_t),
                                       py::format_descriptor<uint8_t>::format(),
                                       3,
                                       { height, width, 4 },
                                       { static_cast<py::ssize_t> (renderer.stride()),
                                         static_cast<py::ssize_t> (4),
                                         static_cast<py::ssize_t> (1) }),
                      capsule);
}

void registerRiveOffscreenRendererBindings (py::module_& m)
{
    py::class_<yup::RiveOffscreenRenderer::Options> options (m, "RiveOffscreenRendererOptions");
    options.def (py::init<>())
        .def_readwrite ("width", &yup::RiveOffscreenRenderer::Options::width)
        .def_readwrite ("height", &yup::RiveOffscreenRenderer::Options::height)
        .def_readwrite ("disable_raster_ordering", &yup::RiveOffscreenRenderer::Options::disableRasterOrdering);

    py::class_<yup::RiveOffscreenRenderer> renderer (m, "RiveOffscreenRenderer");
    renderer.def (py::init<yup::RiveOffscreenRenderer::Options>(), py::arg ("options") = yup::RiveOffscreenRenderer::Options{})
        .def ("load_file",
              [](yup::RiveOffscreenRenderer& self,
                 const std::string& path,
                 std::optional<std::string> artboard)
              {
                  py::gil_scoped_release release;
                  self.loadFromFile (path, artboard ? std::optional<std::string_view> (*artboard) : std::nullopt);
              },
              py::arg ("path"),
              py::arg ("artboard") = std::nullopt)
        .def ("load_bytes",
              [](yup::RiveOffscreenRenderer& self,
                 py::bytes bytes,
                 std::optional<std::string> artboard)
              {
                  std::string buffer = bytes;
                  std::vector<uint8_t> data (buffer.begin(), buffer.end());
                  py::gil_scoped_release release;
                  self.loadFromData (data, artboard ? std::optional<std::string_view> (*artboard) : std::nullopt);
              },
              py::arg ("bytes"),
              py::arg ("artboard") = std::nullopt)
        .def ("animation_names",
              [](const yup::RiveOffscreenRenderer& self)
              {
                  return self.animationNames();
              })
        .def ("state_machine_names",
              [](const yup::RiveOffscreenRenderer& self)
              {
                  return self.stateMachineNames();
              })
        .def ("play_animation",
              [](yup::RiveOffscreenRenderer& self, const std::string& name, bool loop)
              {
                  return self.playAnimation (name, loop);
              },
              py::arg ("name"),
              py::arg ("loop") = true)
        .def ("play_state_machine",
              [](yup::RiveOffscreenRenderer& self, const std::string& name)
              {
                  return self.playStateMachine (name);
              },
              py::arg ("name"))
        .def ("stop", &yup::RiveOffscreenRenderer::stop)
        .def ("set_number_input",
              [](yup::RiveOffscreenRenderer& self, const std::string& name, float value)
              {
                  return self.setNumberInput (name, value);
              })
        .def ("set_boolean_input",
              [](yup::RiveOffscreenRenderer& self, const std::string& name, bool value)
              {
                  return self.setBooleanInput (name, value);
              })
        .def ("fire_trigger",
              [](yup::RiveOffscreenRenderer& self, const std::string& name)
              {
                  return self.fireTrigger (name);
              })
        .def ("advance",
              [](yup::RiveOffscreenRenderer& self, float deltaSeconds)
              {
                  py::gil_scoped_release release;
                  return self.advance (deltaSeconds);
              },
              py::arg ("delta_seconds") = 1.0f / 60.0f)
        .def ("frame", [](yup::RiveOffscreenRenderer& self) { return makeFrameArray (self); })
        .def_property_readonly ("width", &yup::RiveOffscreenRenderer::width)
        .def_property_readonly ("height", &yup::RiveOffscreenRenderer::height);
}

} // namespace yup::Bindings

