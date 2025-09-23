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

#define YUP_PYTHON_INCLUDE_PYBIND11_STL
#define YUP_PYTHON_INCLUDE_PYBIND11_FUNCTIONAL
#define YUP_PYTHON_INCLUDE_PYBIND11_NUMPY
#include "modules/yup_python/utilities/yup_PyBind11Includes.h"

#include "modules/yup_core/files/yup_File.h"
#include "modules/yup_core/misc/yup_Result.h"
#include "modules/yup_core/containers/yup_Span.h"
#include "modules/yup_gui/artboard/yup_RiveOffscreenRenderer.h"

#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace yup
{
namespace python
{
namespace
{
    namespace py = pybind11;
    using namespace pybind11::literals;

    [[noreturn]] void throwResultError (const Result& result)
    {
        throw py::value_error (result.getErrorMessage().toStdString());
    }

    void handleResult (const Result& result)
    {
        if (result.failed())
            throwResultError (result);
    }

    std::vector<std::string> toStdVector (const StringArray& array)
    {
        std::vector<std::string> names;
        names.reserve (static_cast<std::size_t> (array.size()));

        for (int index = 0; index < array.size(); ++index)
            names.emplace_back (array[index].toStdString());

        return names;
    }

    py::memoryview makeFrameMemoryView (const RiveOffscreenRenderer& renderer)
    {
        auto frame = renderer.getFrameBufferShared();

        if (! frame || frame->empty() || renderer.getWidth() <= 0 || renderer.getHeight() <= 0)
        {
            static uint8 dummy = 0;

            return py::memoryview::from_buffer (
                &dummy,
                sizeof (uint8),
                std::vector<py::ssize_t> { 0 },
                std::vector<py::ssize_t> { 1 },
                true);
        }

        auto* data = const_cast<uint8*> (frame->data());
        const auto width = static_cast<py::ssize_t> (renderer.getWidth());
        const auto height = static_cast<py::ssize_t> (renderer.getHeight());
        const auto stride = static_cast<py::ssize_t> (renderer.getRowStride());

        auto capsule = py::capsule (
            new std::shared_ptr<const std::vector<uint8>> (frame),
            [] (void* pointer)
            {
                delete reinterpret_cast<std::shared_ptr<const std::vector<uint8>>*> (pointer);
            });

        return py::memoryview::from_buffer (
            data,
            sizeof (uint8),
            std::vector<py::ssize_t> { height, width, 4 },
            std::vector<py::ssize_t> { stride, 4, 1 },
            true,
            capsule);
    }

    std::vector<uint8> copyBuffer (py::buffer buffer)
    {
        const auto info = buffer.request();

        if (info.ndim != 1)
            throw py::value_error ("Expected a contiguous 1D buffer of bytes");

        std::vector<uint8> bytes (static_cast<std::size_t> (info.size) * static_cast<std::size_t> (info.itemsize));

        if (! bytes.empty())
            std::memcpy (bytes.data(), info.ptr, bytes.size());

        return bytes;
    }

    void bindRiveOffscreenRenderer (py::module_& module)
    {
        namespace py = pybind11;

        py::class_<RiveOffscreenRenderer> renderer (
            module,
            "RiveOffscreenRenderer",
            "Direct3D11-backed offscreen renderer for Rive artboards."
            " Provides animation and state-machine control while exposing BGRA frame"
            " buffers to Python callers.");

        renderer
            .def (
                py::init<int, int>(),
                "width"_a,
                "height"_a,
                "Creates a renderer with the specified output dimensions.")
            .def (
                "is_valid",
                &RiveOffscreenRenderer::isValid,
                "Returns true when the underlying GPU resources were initialised.")
            .def (
                "load_file",
                [] (RiveOffscreenRenderer& self, const std::string& path, std::optional<std::string> artboard)
                {
                    auto result = self.load (File (String (path)), artboard ? String (*artboard) : String());
                    handleResult (result);
                },
                "path"_a,
                "artboard"_a = std::nullopt,
                "Loads a .riv file from disk, optionally selecting an artboard by name.")
            .def (
                "load_bytes",
                [] (RiveOffscreenRenderer& self, py::buffer buffer, std::optional<std::string> artboard)
                {
                    auto bytes = copyBuffer (std::move (buffer));
                    auto result = self.loadFromBytes (
                        Span<const uint8> (bytes.data(), bytes.size()),
                        artboard ? String (*artboard) : String());
                    handleResult (result);
                },
                "data"_a,
                "artboard"_a = std::nullopt,
                "Loads a .riv file from a bytes-like object.")
            .def (
                "list_artboards",
                [] (const RiveOffscreenRenderer& self)
                {
                    return toStdVector (self.listArtboards());
                },
                "Returns the artboards available in the loaded file.")
            .def (
                "list_animations",
                [] (const RiveOffscreenRenderer& self)
                {
                    return toStdVector (self.listAnimations());
                },
                "Returns animations defined on the active artboard.")
            .def (
                "list_state_machines",
                [] (const RiveOffscreenRenderer& self)
                {
                    return toStdVector (self.listStateMachines());
                },
                "Returns state machines defined on the active artboard.")
            .def (
                "select_artboard",
                [] (RiveOffscreenRenderer& self, const std::string& artboard)
                {
                    handleResult (self.selectArtboard (String (artboard)));
                },
                "artboard"_a,
                "Selects an artboard by name.")
            .def (
                "get_active_artboard",
                &RiveOffscreenRenderer::getActiveArtboardName,
                "Returns the name of the current artboard, or an empty string if none is active.")
            .def (
                "play_animation",
                [] (RiveOffscreenRenderer& self, const std::string& animation, bool shouldLoop)
                {
                    return self.playAnimation (String (animation), shouldLoop);
                },
                "animation"_a,
                "loop"_a = true,
                "Starts playing a linear animation and returns true on success.")
            .def (
                "play_state_machine",
                [] (RiveOffscreenRenderer& self, const std::string& machine)
                {
                    return self.playStateMachine (String (machine));
                },
                "machine"_a,
                "Starts playing a state machine and returns true on success.")
            .def ("stop", &RiveOffscreenRenderer::stop, "Stops any running animation or state machine.")
            .def (
                "set_paused",
                &RiveOffscreenRenderer::setPaused,
                "paused"_a,
                "Pauses or resumes advancement of the active scene.")
            .def (
                "is_paused",
                &RiveOffscreenRenderer::isPaused,
                "Returns true when the renderer is paused.")
            .def (
                "set_bool_input",
                [] (RiveOffscreenRenderer& self, const std::string& name, bool value)
                {
                    return self.setBoolInput (String (name), value);
                },
                "name"_a,
                "value"_a,
                "Sets a boolean state-machine input and returns true if it existed.")
            .def (
                "set_number_input",
                [] (RiveOffscreenRenderer& self, const std::string& name, double value)
                {
                    return self.setNumberInput (String (name), value);
                },
                "name"_a,
                "value"_a,
                "Sets a numeric state-machine input and returns true if it existed.")
            .def (
                "fire_trigger",
                [] (RiveOffscreenRenderer& self, const std::string& name)
                {
                    return self.fireTriggerInput (String (name));
                },
                "name"_a,
                "Fires a trigger state-machine input and returns true if it existed.")
            .def (
                "advance",
                &RiveOffscreenRenderer::advance,
                "delta_seconds"_a,
                "Advances the current scene by the given time and renders a new frame.")
            .def (
                "get_width",
                &RiveOffscreenRenderer::getWidth,
                "Returns the width of the offscreen surface in pixels.")
            .def (
                "get_height",
                &RiveOffscreenRenderer::getHeight,
                "Returns the height of the offscreen surface in pixels.")
            .def (
                "get_row_stride",
                &RiveOffscreenRenderer::getRowStride,
                "Returns the stride in bytes between rows of the frame buffer.")
            .def (
                "get_frame_bytes",
                [] (const RiveOffscreenRenderer& self)
                {
                    const auto& frame = self.getFrameBuffer();
                    return py::bytes (reinterpret_cast<const char*> (frame.data()), static_cast<py::ssize_t> (frame.size()));
                },
                "Returns a copy of the most recent frame as bytes in BGRA order.")
            .def (
                "acquire_frame_view",
                [] (const RiveOffscreenRenderer& self)
                {
                    return makeFrameMemoryView (self);
                },
                "Returns a read-only memoryview that references the renderer's BGRA frame without copying.")
            .def (
                "get_last_error",
                &RiveOffscreenRenderer::getLastError,
                "Returns the last error message reported by the renderer.");
    }
}

PYBIND11_MODULE (yup_rive_renderer, module)
{
    namespace py = pybind11;
    module.doc () =
        "Bindings that expose yup::RiveOffscreenRenderer to Python callers."
        " The module is designed for Windows 11 workflows using Direct3D11.";

    python::bindRiveOffscreenRenderer (module);
}

} // namespace python
} // namespace yup

