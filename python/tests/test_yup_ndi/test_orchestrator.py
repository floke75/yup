"""
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
"""

from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction
from types import SimpleNamespace
from typing import Any, Dict, Mapping, Optional

import pytest

from yup_ndi import NDIOrchestrator, NDIStreamConfig
import yup_ndi.orchestrator as orchestrator_module


class FakeRenderer:
    def __init__ (self, width: int, height: int) -> None:
        self._width = width
        self._height = height
        self._row_stride = width * 4
        self.loaded_file: Optional[str] = None
        self.loaded_bytes: Optional[bytes] = None
        self.loaded_artboard: Optional[str] = None
        self.animations: list[tuple[str, bool]] = []
        self.state_machines: list[str] = []
        self.bool_inputs: Dict[str, bool] = {}
        self.number_inputs: Dict[str, float] = {}
        self.triggers: list[str] = []
        self.selected_artboard: Optional[str] = None
        self.advanced: list[float] = []
        self.paused = False
        self.stopped = False
        self._frame = bytes(range(width * height * 4))

    def is_valid (self) -> bool:
        return True

    def load_file (self, path: str, artboard: Optional[str]) -> None:
        self.loaded_file = path
        self.loaded_artboard = artboard

    def load_bytes (self, data: bytes, artboard: Optional[str]) -> None:
        self.loaded_bytes = bytes(data)
        self.loaded_artboard = artboard

    def play_animation (self, name: str, loop: bool = True) -> bool:
        self.animations.append((name, loop))
        return True

    def play_state_machine (self, name: str) -> bool:
        self.state_machines.append(name)
        return True

    def select_artboard (self, name: str) -> None:
        self.selected_artboard = name

    def set_paused (self, should_pause: bool) -> None:
        self.paused = should_pause

    def set_bool_input (self, name: str, value: bool) -> bool:
        self.bool_inputs[name] = value
        return True

    def set_number_input (self, name: str, value: float) -> bool:
        self.number_inputs[name] = value
        return True

    def fire_trigger_input (self, name: str) -> bool:
        self.triggers.append(name)
        return True

    def fire_trigger (self, name: str) -> bool:
        # Legacy name maintained to verify fallback behaviour.
        return self.fire_trigger_input(name)

    def advance (self, delta_seconds: float) -> bool:
        self.advanced.append(delta_seconds)
        return True

    def get_frame_bytes (self) -> bytes:
        return self._frame

    def get_width (self) -> int:
        return self._width

    def get_height (self) -> int:
        return self._height

    def get_row_stride (self) -> int:
        return self._row_stride

    def stop (self) -> None:
        self.stopped = True


@dataclass
class FakeSenderCall:
    width: int
    height: int
    stride: int
    timestamp: int
    payload: bytes


class FakeSenderHandle:
    def __init__ (self, name: str) -> None:
        self.name = name
        self.calls: list[FakeSenderCall] = []
        self.metadata: list[Mapping[str, Mapping[str, Any]]] = []
        self.closed = False
        self.connection_count = 1

    def send (self, buffer: memoryview, width: int, height: int, stride: int, timestamp: int) -> None:
        self.calls.append(FakeSenderCall(width, height, stride, timestamp, bytes(buffer)))

    def apply_metadata (self, metadata: Mapping[str, Mapping[str, Any]]) -> None:
        if metadata:
            self.metadata.append(metadata)

    def close (self) -> None:
        self.closed = True

    def get_connection_count (self) -> int:
        return self.connection_count


class FakeFactories:
    def __init__ (self) -> None:
        self.renderers: Dict[str, FakeRenderer] = {}
        self.senders: Dict[str, FakeSenderHandle] = {}

    def renderer (self, config: NDIStreamConfig) -> FakeRenderer:
        renderer = FakeRenderer(config.width, config.height)
        self.renderers[config.name] = renderer
        return renderer

    def sender (self, config: NDIStreamConfig, renderer: FakeRenderer) -> FakeSenderHandle:
        handle = FakeSenderHandle(config.name)
        self.senders[config.name] = handle
        return handle


def test_stream_config_accepts_fraction_ratio () -> None:
    rate = Fraction(60000, 1001)
    config = NDIStreamConfig(name="frac", width=1, height=1, riv_bytes=b"riv", frame_rate=rate)
    assert config.frame_rate is rate


def test_stream_config_rejects_zero_denominator_tuple () -> None:
    with pytest.raises(ValueError):
        NDIStreamConfig(name="invalid", width=1, height=1, riv_bytes=b"riv", frame_rate=(60000, 0))


def test_stream_config_requires_two_tuple_entries () -> None:
    with pytest.raises(TypeError):
        NDIStreamConfig(name="invalid", width=1, height=1, riv_bytes=b"riv", frame_rate=(60000,))


def test_stream_config_rejects_negative_fraction () -> None:
    with pytest.raises(ValueError):
        NDIStreamConfig(name="invalid", width=1, height=1, riv_bytes=b"riv", frame_rate=Fraction(-1, 1))


def test_stream_config_normalises_zero_frame_rate_to_none () -> None:
    config = NDIStreamConfig(name="zero", width=1, height=1, riv_bytes=b"riv", frame_rate=0)
    assert config.frame_rate is None


def test_stream_config_rejects_non_finite_float_frame_rate () -> None:
    with pytest.raises(ValueError):
        NDIStreamConfig(name="nan", width=1, height=1, riv_bytes=b"riv", frame_rate=float("nan"))


def test_orchestrator_advances_and_sends_frames () -> None:
    factories = FakeFactories()
    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        timestamp_mapper=lambda seconds: int(seconds * 1_000),
        time_provider=lambda: 1.25,
    )

    config = NDIStreamConfig(
        name="main",
        width=2,
        height=2,
        riv_bytes=b"riv data",
        animation="run",
        loop_animation=False,
        metadata={"ndi": {"note": "demo"}},
    )

    orchestrator.add_stream(config)

    renderer = factories.renderers["main"]
    assert renderer.loaded_bytes == b"riv data"
    assert renderer.animations == [("run", False)]

    progressed = orchestrator.advance_stream("main", 0.5)
    assert progressed is True
    assert renderer.advanced == [0.5]

    sender = factories.senders["main"]
    assert sender.calls, "Sender should have emitted a frame"
    frame_call = sender.calls[-1]
    assert frame_call.width == 2
    assert frame_call.height == 2
    assert frame_call.stride == renderer.get_row_stride()
    assert frame_call.timestamp == int(1.25 * 1_000)
    assert sender.metadata == [config.metadata]

    metrics = orchestrator.get_stream_metrics("main")
    assert metrics.frames_sent == 1
    assert metrics.frames_suppressed == 0
    assert metrics.last_connection_count == 1


def test_remove_stream_closes_sender () -> None:
    factories = FakeFactories()
    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        timestamp_mapper=lambda seconds: int(seconds * 1_000),
        time_provider=lambda: 0.0,
    )

    config = NDIStreamConfig(name="solo", width=1, height=1, riv_bytes=b"riv")
    orchestrator.add_stream(config)
    orchestrator.advance_all(0.0)

    orchestrator.remove_stream("solo")
    assert factories.senders["solo"].closed is True
    assert factories.renderers["solo"].stopped is True


def test_throttle_skips_send_when_no_connections () -> None:
    factories = FakeFactories()
    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        timestamp_mapper=lambda seconds: int(seconds * 1_000),
        time_provider=lambda: 0.0,
    )

    config = NDIStreamConfig(
        name="idle",
        width=2,
        height=2,
        riv_bytes=b"riv",
        inactive_connection_poll_interval=0.0,
    )

    orchestrator.add_stream(config)
    factories.senders["idle"].connection_count = 0

    progressed = orchestrator.advance_stream("idle", 0.25)
    assert progressed is True

    sender = factories.senders["idle"]
    assert sender.calls == []

    renderer = factories.renderers["idle"]
    assert renderer.advanced == [0.25]

    metrics = orchestrator.get_stream_metrics("idle")
    assert metrics.frames_sent == 0
    assert metrics.frames_suppressed == 1
    assert metrics.last_connection_count == 0
    assert metrics.paused_for_inactivity is False


def test_pause_when_inactive_prevents_advancement () -> None:
    factories = FakeFactories()
    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        timestamp_mapper=lambda seconds: int(seconds * 1_000),
        time_provider=lambda: 0.0,
    )

    config = NDIStreamConfig(
        name="sleepy",
        width=2,
        height=2,
        riv_bytes=b"riv",
        pause_when_inactive=True,
        inactive_connection_poll_interval=0.0,
    )

    orchestrator.add_stream(config)
    sender = factories.senders["sleepy"]
    renderer = factories.renderers["sleepy"]

    sender.connection_count = 0
    orchestrator.advance_stream("sleepy", 0.5)

    assert renderer.advanced == []
    assert renderer.paused is True

    metrics = orchestrator.get_stream_metrics("sleepy")
    assert metrics.frames_sent == 0
    assert metrics.frames_suppressed == 1
    assert metrics.paused_for_inactivity is True

    sender.connection_count = 1
    orchestrator.advance_stream("sleepy", 0.5)

    assert renderer.advanced == [0.5]
    assert renderer.paused is False

    metrics = orchestrator.get_stream_metrics("sleepy")
    assert metrics.frames_sent == 1
    assert metrics.frames_suppressed == 1
    assert metrics.paused_for_inactivity is False


def test_apply_stream_control_handles_common_actions () -> None:
    factories = FakeFactories()
    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        timestamp_mapper=lambda seconds: 0,
        time_provider=lambda: 0.0,
    )

    config = NDIStreamConfig(name="control", width=4, height=4, riv_bytes=b"riv")
    orchestrator.add_stream(config)

    renderer = factories.renderers["control"]

    orchestrator.apply_stream_control("control", "pause")
    assert renderer.paused is True
    orchestrator.apply_stream_control("control", "resume")
    assert renderer.paused is False

    orchestrator.apply_stream_control("control", "play_animation", {"name": "idle", "loop": True})
    orchestrator.apply_stream_control("control", "play_state_machine", {"name": "fsm"})
    orchestrator.apply_stream_control("control", "select_artboard", {"name": "Alt"})

    orchestrator.apply_stream_control("control", "set_input", {"name": "armed", "value": True})
    orchestrator.apply_stream_control("control", "set_input", {"name": "intensity", "value": 0.75})
    orchestrator.apply_stream_control("control", "set_input", {"name": "trigger", "value": "trigger"})
    orchestrator.apply_stream_control("control", "stop")

    assert renderer.animations[-1] == ("idle", True)
    assert renderer.state_machines[-1] == "fsm"
    assert renderer.selected_artboard == "Alt"
    assert renderer.bool_inputs["armed"] is True
    assert renderer.number_inputs["intensity"] == pytest.approx(0.75)
    assert renderer.triggers == ["trigger"]
    assert renderer.stopped is True

    with pytest.raises(ValueError):
        orchestrator.apply_stream_control("control", "unknown")


def test_apply_stream_metadata_delegates_to_sender () -> None:
    factories = FakeFactories()
    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        timestamp_mapper=lambda seconds: 0,
        time_provider=lambda: 0.0,
    )

    config = NDIStreamConfig(name="meta", width=2, height=2, riv_bytes=b"riv")
    orchestrator.add_stream(config)

    orchestrator.apply_stream_metadata("meta", {"ndi": {"note": "update"}})
    sender = factories.senders["meta"]
    assert sender.metadata[-1] == {"ndi": {"note": "update"}}


def test_initial_state_machine_inputs_are_applied () -> None:
    factories = FakeFactories()
    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        timestamp_mapper=lambda seconds: 0,
        time_provider=lambda: 0.0,
    )

    config = NDIStreamConfig(
        name="init",
        width=2,
        height=2,
        riv_bytes=b"riv",
        state_machine="controller",
        state_machine_inputs={"armed": True, "level": 0.5, "bang": "trigger"},
    )

    orchestrator.add_stream(config)

    renderer = factories.renderers["init"]
    assert renderer.state_machines == ["controller"]
    assert renderer.bool_inputs["armed"] is True
    assert renderer.number_inputs["level"] == pytest.approx(0.5)
    assert renderer.triggers == ["bang"]


def test_initial_input_failure_raises_runtime_error () -> None:
    class RejectingRenderer(FakeRenderer):
        def set_bool_input (self, name: str, value: bool) -> bool:
            super().set_bool_input(name, value)
            return False

    class RejectingFactories(FakeFactories):
        def renderer (self, config: NDIStreamConfig) -> FakeRenderer:
            renderer = RejectingRenderer(config.width, config.height)
            self.renderers[config.name] = renderer
            return renderer

    factories = RejectingFactories()
    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        timestamp_mapper=lambda seconds: 0,
        time_provider=lambda: 0.0,
    )

    config = NDIStreamConfig(
        name="reject",
        width=2,
        height=2,
        riv_bytes=b"riv",
        state_machine="controller",
        state_machine_inputs={"armed": True},
    )

    with pytest.raises(RuntimeError):
        orchestrator.add_stream(config)


def test_dispatch_control_invokes_registered_handler () -> None:
    factories = FakeFactories()
    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        timestamp_mapper=lambda seconds: 0,
        time_provider=lambda: 0.0,
    )

    orchestrator.add_stream(NDIStreamConfig(name="a", width=1, height=1, riv_bytes=b"riv"))
    orchestrator.add_stream(NDIStreamConfig(name="b", width=1, height=1, riv_bytes=b"riv"))

    dispatched: list[tuple[str, Mapping[str, Any]]] = []

    def handler (orc: NDIOrchestrator, command: str, payload: Mapping[str, Any]) -> str:
        dispatched.append((command, payload))
        for stream_name in orc.list_streams():
            orc.apply_stream_control(stream_name, payload["action"], payload.get("parameters", {}))
        return "ok"

    orchestrator.register_control_handler("toggle", handler)
    result = orchestrator.dispatch_control("toggle", {"action": "pause"})

    assert result == "ok"
    assert dispatched == [("toggle", {"action": "pause"})]
    assert all(factories.renderers[name].paused for name in ("a", "b"))

    orchestrator.unregister_control_handler("toggle")
    with pytest.raises(KeyError):
        orchestrator.dispatch_control("toggle")


def test_invalid_configuration_rejected () -> None:
    with pytest.raises(ValueError):
        NDIStreamConfig(name="bad", width=0, height=10, riv_bytes=b"riv")

    with pytest.raises(ValueError):
        NDIStreamConfig(name="bad", width=1, height=1)

    with pytest.raises(TypeError):
        NDIStreamConfig(name="bad", width=1, height=1, riv_bytes=b"riv", frame_rate="fast")  # type: ignore[arg-type]


def test_acquire_frame_view_prefers_zero_copy_memoryview () -> None:
    class ViewRenderer(FakeRenderer):
        def __init__ (self, width: int, height: int) -> None:
            super().__init__(width, height)
            self._buffer = bytearray(range(width * height * 4))
            self.get_bytes_called = False

        def acquire_frame_view (self) -> memoryview:
            return memoryview(self._buffer).cast("B", (self._height, self._width, 4))

        def get_frame_bytes (self) -> bytes:
            self.get_bytes_called = True
            return bytes(self._buffer)

    class RecordingSender:
        def __init__ (self) -> None:
            self.buffers: list[memoryview] = []
            self.dimensions: list[tuple[int, int, int]] = []

        def send (self, buffer: memoryview, width: int, height: int, stride: int, timestamp: int) -> None:
            self.buffers.append(buffer)
            self.dimensions.append((width, height, stride))

        def apply_metadata (self, metadata: Mapping[str, Mapping[str, Any]]) -> None:
            pass

        def close (self) -> None:
            pass

    sender = RecordingSender()

    orchestrator = NDIOrchestrator(
        renderer_factory=lambda config: ViewRenderer(config.width, config.height),
        sender_factory=lambda config, renderer: sender,
        timestamp_mapper=lambda seconds: int(seconds * 1_000_000),
        time_provider=lambda: 0.0,
    )

    config = NDIStreamConfig(name="view", width=2, height=2, riv_bytes=b"riv")
    orchestrator.add_stream(config)

    progressed = orchestrator.advance_stream("view", 0.1)
    assert progressed is True

    assert sender.buffers, "Expected sender to receive a memoryview"
    buffer = sender.buffers[-1]
    assert isinstance(buffer, memoryview)
    assert buffer.format == "B"
    assert buffer.readonly is False
    assert buffer.obj is orchestrator._streams["view"].renderer._buffer  # type: ignore[attr-defined]
    assert bytes(buffer) == bytes(range(16))

    renderer = orchestrator._streams["view"].renderer  # type: ignore[assignment]
    assert isinstance(renderer, ViewRenderer)
    assert renderer.get_bytes_called is False


def test_stream_close_stops_renderer_even_if_sender_close_fails () -> None:
    class StopAwareRenderer(FakeRenderer):
        def __init__ (self, width: int, height: int) -> None:
            super().__init__(width, height)
            self.stop_calls = 0

        def stop (self) -> None:
            self.stop_calls += 1

    class FaultySender:
        def send (self, buffer: memoryview, width: int, height: int, stride: int, timestamp: int) -> None:
            pass

        def apply_metadata (self, metadata: Mapping[str, Mapping[str, Any]]) -> None:
            pass

        def close (self) -> None:
            raise RuntimeError("boom")

    renderer = StopAwareRenderer(2, 2)
    orchestrator = NDIOrchestrator(
        renderer_factory=lambda config: renderer,
        sender_factory=lambda config, renderer: FaultySender(),
        timestamp_mapper=lambda seconds: 0,
        time_provider=lambda: 0.0,
    )

    orchestrator.add_stream(NDIStreamConfig(name="unstable", width=2, height=2, riv_bytes=b"riv"))

    with pytest.raises(RuntimeError):
        orchestrator.remove_stream("unstable")

    assert renderer.stop_calls == 1
    assert orchestrator.list_streams() == []


def test_default_factories_wire_renderer_and_sender (monkeypatch: pytest.MonkeyPatch) -> None:
    created_renderers: list[Any] = []
    sender_handles: list[Any] = []

    class StubRenderer:
        def __init__ (self, width: int, height: int, staging_buffer_count: int = 1) -> None:
            self.width = width
            self.height = height
            self.row_stride = width * 4
            self.staging_buffer_count = staging_buffer_count
            self.loaded: Optional[bytes] = None
            self.artboard: Optional[str] = None
            self.played_animation: Optional[tuple[str, bool]] = None
            self.played_state_machine: Optional[str] = None
            self.paused = True
            self.advanced: list[float] = []
            self.stopped = False
            self._frame = bytearray(range(width * height * 4))
            created_renderers.append(self)

        def is_valid (self) -> bool:
            return True

        def load_file (self, path: str, artboard: Optional[str]) -> None:
            raise AssertionError("load_file should not be called in this test")

        def load_bytes (self, data: bytes, artboard: Optional[str]) -> None:
            self.loaded = bytes(data)
            self.artboard = artboard

        def play_animation (self, name: str, loop: bool = True) -> bool:
            self.played_animation = (name, loop)
            return True

        def play_state_machine (self, name: str) -> bool:
            self.played_state_machine = name
            return True

        def set_paused (self, value: bool) -> None:
            self.paused = value

        def advance (self, delta_seconds: float) -> bool:
            self.advanced.append(delta_seconds)
            return True

        def get_width (self) -> int:
            return self.width

        def get_height (self) -> int:
            return self.height

        def get_row_stride (self) -> int:
            return self.row_stride

        def get_frame_bytes (self) -> memoryview:
            return memoryview(self._frame)

        def stop (self) -> None:
            self.stopped = True

    class StubVideoFrame:
        def __init__ (self) -> None:
            self.fourcc: Optional[str] = None
            self.resolutions: list[tuple[int, int]] = []
            self.frame_rate: Optional[Fraction] = None
            self.ptr = SimpleNamespace(timestamp=None)

        def set_fourcc (self, fourcc: str) -> None:
            self.fourcc = fourcc

        def set_resolution (self, width: int, height: int) -> None:
            self.resolutions.append((width, height))

        def set_frame_rate (self, frame_rate: Fraction) -> None:
            self.frame_rate = frame_rate

    class StubSender:
        def __init__ (self, name: str, ndi_groups: str = "", **kwargs: Any) -> None:
            self.name = name
            self.ndi_groups = ndi_groups
            self.kwargs = kwargs
            self.video_frame: Optional[StubVideoFrame] = None
            self.open_called = False
            self.video_payloads: list[memoryview] = []
            self.sent_async = False
            self.sent_sync = False
            self.metadata: list[tuple[str, Mapping[str, Any]]] = []
            self.closed = False
            sender_handles.append(self)

        def set_video_frame (self, frame: StubVideoFrame) -> None:
            self.video_frame = frame

        def open (self) -> None:
            self.open_called = True

        def write_video (self, buffer: memoryview) -> None:
            self.video_payloads.append(buffer)

        def send_video_async (self) -> None:
            self.sent_async = True

        def send_video (self) -> None:
            self.sent_sync = True

        def send_metadata (self, tag: str, attrs: Mapping[str, Any]) -> None:
            self.metadata.append((tag, dict(attrs)))

        def close (self) -> None:
            self.closed = True

    class StubFourCC:
        BGRA = "BGRA"

    class StubCyndiLib:
        Sender = StubSender
        VideoSendFrame = StubVideoFrame
        FourCC = StubFourCC

    monkeypatch.setattr(orchestrator_module, "RiveOffscreenRenderer", StubRenderer)
    monkeypatch.setattr(orchestrator_module, "cyndilib", StubCyndiLib())

    orchestrator = NDIOrchestrator()

    config = NDIStreamConfig(
        name="default",
        width=2,
        height=2,
        riv_bytes=b"riv bytes",
        animation="spin",
        loop_animation=False,
        metadata={"ndi": {"title": "demo"}},
        frame_rate=Fraction(30000, 1001),
    )

    orchestrator.add_stream(config)

    assert created_renderers, "Renderer factory should have been invoked"
    renderer = created_renderers[-1]
    assert renderer.loaded == b"riv bytes"
    assert renderer.artboard is None
    assert renderer.played_animation == ("spin", False)
    assert renderer.played_state_machine is None
    assert renderer.paused is False
    assert renderer.staging_buffer_count == 1

    assert sender_handles, "Sender factory should have been invoked"
    sender = sender_handles[-1]
    assert sender.video_frame is not None
    assert sender.video_frame.fourcc == "BGRA"
    assert sender.video_frame.resolutions[-1] == (2, 2)
    assert sender.video_frame.frame_rate == Fraction(30000, 1001)
    assert sender.open_called is True

    progressed = orchestrator.advance_stream("default", 0.25, timestamp=1.0)
    assert progressed is True
    assert renderer.advanced == [0.25]
    assert sender.video_payloads, "Sender should have received a frame payload"
    payload = sender.video_payloads[-1]
    assert isinstance(payload, memoryview)
    assert bytes(payload) == bytes(range(16))
    assert sender.sent_async is True
    assert sender.sent_sync is False

    assert sender.metadata == [("ndi", {"title": "demo"})]

    orchestrator.remove_stream("default")

    assert renderer.stopped is True
    assert sender.closed is True


def test_fractional_frame_rate_produces_evenly_spaced_timestamps () -> None:
    factories = FakeFactories()
    base_time = 10.0
    fps = Fraction(30000, 1001)
    period_seconds = float(Fraction(1001, 30000))

    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        time_provider=lambda: base_time,
    )

    config = NDIStreamConfig(
        name="fractional",
        width=2,
        height=2,
        riv_bytes=b"riv bytes",
        frame_rate=fps,
    )

    orchestrator.add_stream(config)

    for index in range(5):
        timestamp = base_time + period_seconds * index
        start_timestamp = base_time if index == 0 else None
        orchestrator.advance_stream(
            "fractional",
            period_seconds,
            timestamp=timestamp,
            start_timestamp=start_timestamp,
        )

    sender = factories.senders["fractional"]
    timestamps = [call.timestamp for call in sender.calls]
    assert len(timestamps) == 5

    anchor_100ns = int(base_time * 10_000_000)
    frame_period_100ns = Fraction(10_000_000, 1) / fps
    expected = [int(Fraction(anchor_100ns, 1) + frame_period_100ns * i) for i in range(5)]

    assert timestamps == expected


def test_set_stream_start_time_primes_anchor () -> None:
    factories = FakeFactories()
    base_time = 12.5
    fps = Fraction(30000, 1001)
    period_seconds = float(Fraction(1001, 30000))

    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        time_provider=lambda: base_time + period_seconds,
    )

    config = NDIStreamConfig(
        name="anchored",
        width=2,
        height=2,
        riv_bytes=b"riv bytes",
        frame_rate=fps,
    )

    orchestrator.add_stream(config)
    orchestrator.set_stream_start_time("anchored", base_time)

    orchestrator.advance_stream(
        "anchored",
        period_seconds,
        timestamp=base_time + period_seconds,
    )

    sender = factories.senders["anchored"]
    assert sender.calls, "Expected a frame send"

    first_timestamp = sender.calls[0].timestamp
    assert first_timestamp == int(base_time * 10_000_000)


def test_default_renderer_factory_respects_staging_buffer_count (monkeypatch: pytest.MonkeyPatch) -> None:
    captured: dict[str, tuple[int, int, int]] = {}

    class StubRenderer:
        def __init__ (self, width: int, height: int, staging_buffer_count: int = 1) -> None:
            captured["args"] = (width, height, staging_buffer_count)

        def is_valid (self) -> bool:
            return True

    monkeypatch.setattr(orchestrator_module, "RiveOffscreenRenderer", StubRenderer)

    config = NDIStreamConfig(
        name="buffered",
        width=2,
        height=2,
        riv_bytes=b"riv",
        renderer_options={"staging_buffer_count": 4},
    )

    renderer = orchestrator_module._default_renderer_factory(config)

    assert isinstance(renderer, StubRenderer)
    assert captured["args"] == (2, 2, 4)


def test_default_renderer_factory_validates_staging_buffer_count (monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(orchestrator_module, "RiveOffscreenRenderer", object)

    config = NDIStreamConfig(
        name="invalid",
        width=2,
        height=2,
        riv_bytes=b"riv",
        renderer_options={"staging_buffer_count": 0},
    )

    with pytest.raises(ValueError):
        orchestrator_module._default_renderer_factory(config)


def test_default_renderer_factory_surfaces_renderer_error (monkeypatch: pytest.MonkeyPatch) -> None:
    class StubRenderer:
        def __init__ (self, width: int, height: int, staging_buffer_count: int = 1) -> None:
            assert staging_buffer_count == 1
            self._width = width
            self._height = height

        def is_valid (self) -> bool:
            return False

        def get_last_error (self) -> str:
            return "Device lost"

    monkeypatch.setattr(orchestrator_module, "RiveOffscreenRenderer", StubRenderer)

    config = NDIStreamConfig(
        name="broken",
        width=1920,
        height=1080,
        riv_bytes=b"riv",
    )

    with pytest.raises(RuntimeError) as excinfo:
        orchestrator_module._default_renderer_factory(config)

    message = str(excinfo.value)
    assert "Failed to initialise RiveOffscreenRenderer for 1920x1080" in message
    assert "Device lost" in message
