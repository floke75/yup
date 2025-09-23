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

#include <yup_rive_renderer/yup_rive_renderer.h>

#include "../utilities/yup_NumPyUtilities.h"

#include <algorithm>
#include <array>
#include <cstring>

#include <yup_core/file/yup_File.h>

namespace yup::Bindings
{
namespace
{
pybind11::array makeFrameArray (yup::rive_renderer::RiveRenderer& renderer)
{
    const auto width = renderer.width();
    const auto height = renderer.height();

    if (width == 0 || height == 0)
        return pybind11::array (pybind11::dtype::of<uint8>(), { 0, 0, 4 });

    auto data = renderer.pixelData();
    const auto shape = std::array<pybind11::ssize_t, 3> {
        static_cast<pybind11::ssize_t> (height),
        static_cast<pybind11::ssize_t> (width),
        4
    };

    const auto strides = std::array<pybind11::ssize_t, 3> {
        static_cast<pybind11::ssize_t> (width * 4),
        4,
        1
    };

    pybind11::array result (pybind11::dtype::of<uint8>(), shape, strides);
    const auto bytesToCopy = std::min (static_cast<size_t> (result.nbytes()), data.size());
    std::memcpy (result.mutable_data(), data.data(), bytesToCopy);
    return result;
}
}

void registerYupRiveRendererBindings (pybind11::module_& module)
{
    namespace py = pybind11;
    using namespace yup::rive_renderer;

    py::class_<RiveRenderer> (module, "RiveRenderer")
        .def (py::init<>())
        .def ("load", [] (RiveRenderer& self, const std::string& path) {
            return self.load (yup::File (path));
        })
        .def ("load_bytes", [] (RiveRenderer& self, py::bytes bytes) {
            const auto buffer = bytes.cast<std::string>();
            return self.loadFromData (Span<const uint8> {
                reinterpret_cast<const uint8*> (buffer.data()),
                static_cast<size_t> (buffer.size()) });
        })
        .def ("animations", &RiveRenderer::animationNames)
        .def ("state_machines", &RiveRenderer::stateMachineNames)
        .def ("play", &RiveRenderer::playAnimation, py::arg ("name"), py::arg ("loop") = true)
        .def ("play_state_machine", &RiveRenderer::playStateMachine)
        .def ("stop", &RiveRenderer::stop)
        .def ("pause", &RiveRenderer::pause)
        .def ("resume", &RiveRenderer::resume)
        .def ("advance", &RiveRenderer::advance, py::arg ("dt"), py::call_guard<py::gil_scoped_release>())
        .def ("frame", [] (RiveRenderer& self) { return makeFrameArray (self); })
        .def_property_readonly ("width", &RiveRenderer::width)
        .def_property_readonly ("height", &RiveRenderer::height)
        .def_property_readonly ("current_animation", &RiveRenderer::currentAnimation)
        .def_property_readonly ("current_state_machine", &RiveRenderer::currentStateMachine)
        .def ("set_number", &RiveRenderer::setNumberInput)
        .def ("set_bool", &RiveRenderer::setBooleanInput)
        .def ("fire_trigger", &RiveRenderer::fireTrigger)
        .def ("render", &RiveRenderer::renderFrame, py::call_guard<py::gil_scoped_release>());
}

} // namespace yup::Bindings
