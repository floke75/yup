from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, Mapping, Optional

import pytest

import yup_ndi.orchestrator as orchestrator_module
from yup_ndi import NDIOrchestrator, NDIStreamConfig


class FakeRenderer:
    def __init__ (self, width: int, height: int, *, expose_view: bool = False) -> None:
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
        self._frame_bytes = bytes(range(width * height * 4))
        self._frame_array = bytearray(self._frame_bytes)
        self._use_view = expose_view
        self.view_requests = 0
        self.byte_requests = 0

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

    def fire_trigger (self, name: str) -> bool:
        self.triggers.append(name)
        return True

    def advance (self, delta_seconds: float) -> bool:
        self.advanced.append(delta_seconds)
        return True

    def get_frame_bytes (self) -> bytes:
        self.byte_requests += 1
        return self._frame_bytes

    def acquire_frame_view (self) -> memoryview:
        if not self._use_view:
            raise AttributeError("acquire_frame_view not available")

        self.view_requests += 1
        return memoryview(self._frame_array).cast("B", shape=(self._height, self._width, 4))

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

    def send (self, buffer: memoryview, width: int, height: int, stride: int, timestamp: int) -> None:
        self.calls.append(FakeSenderCall(width, height, stride, timestamp, bytes(buffer)))

    def apply_metadata (self, metadata: Mapping[str, Mapping[str, Any]]) -> None:
        if metadata:
            self.metadata.append(metadata)

    def close (self) -> None:
        self.closed = True


class FakeFactories:
    def __init__ (self) -> None:
        self.renderers: Dict[str, FakeRenderer] = {}
        self.senders: Dict[str, FakeSenderHandle] = {}

    def renderer (self, config: NDIStreamConfig) -> FakeRenderer:
        expose_view = bool(config.renderer_options.get("expose_view"))
        renderer = FakeRenderer(config.width, config.height, expose_view=expose_view)
        self.renderers[config.name] = renderer
        return renderer

    def sender (self, config: NDIStreamConfig, renderer: FakeRenderer) -> FakeSenderHandle:
        handle = FakeSenderHandle(config.name)
        self.senders[config.name] = handle
        return handle


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


def test_orchestrator_prefers_acquire_frame_view () -> None:
    factories = FakeFactories()
    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        timestamp_mapper=lambda seconds: int(seconds * 1_000),
        time_provider=lambda: 0.0,
    )

    config = NDIStreamConfig(
        name="view", width=2, height=2, riv_bytes=b"data", renderer_options={"expose_view": True}
    )

    orchestrator.add_stream(config)

    renderer = factories.renderers["view"]
    sender = factories.senders["view"]

    orchestrator.advance_stream("view", 0.25)

    assert renderer.view_requests == 1
    assert renderer.byte_requests == 0, "acquire_frame_view should prevent fallback to get_frame_bytes"
    payload = sender.calls[0].payload
    assert payload == bytes(range(16))


def test_orchestrator_remove_stream_closes_resources () -> None:
    factories = FakeFactories()
    orchestrator = NDIOrchestrator(
        renderer_factory=factories.renderer,
        sender_factory=factories.sender,
        timestamp_mapper=lambda seconds: int(seconds * 1_000),
        time_provider=lambda: 0.0,
    )

    config = NDIStreamConfig(name="cleanup", width=2, height=2, riv_bytes=b"riv data")

    orchestrator.add_stream(config)
    orchestrator.remove_stream("cleanup")

    renderer = factories.renderers["cleanup"]
    sender = factories.senders["cleanup"]

    assert renderer.stopped is True
    assert sender.closed is True


def test_default_renderer_factory_calls_binding (monkeypatch: pytest.MonkeyPatch) -> None:
    created_dimensions: list[tuple[int, int]] = []

    class DummyRenderer:
        valid = True

        def __init__ (self, width: int, height: int) -> None:
            created_dimensions.append((width, height))

        def is_valid (self) -> bool:
            return type(self).valid

    monkeypatch.setattr(orchestrator_module, "RiveOffscreenRenderer", DummyRenderer)

    config = NDIStreamConfig(name="binding", width=1920, height=1080, riv_bytes=b"riv")
    renderer = orchestrator_module._default_renderer_factory(config)

    assert isinstance(renderer, DummyRenderer)
    assert created_dimensions == [(1920, 1080)]

    DummyRenderer.valid = False
    with pytest.raises(RuntimeError):
        orchestrator_module._default_renderer_factory(config)


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
