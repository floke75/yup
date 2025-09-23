"""Utilities for publishing Rive frames over NDI."""
from __future__ import annotations

from dataclasses import dataclass, field
from fractions import Fraction
import logging
import time
from typing import Any, Callable, Dict, List, Mapping, MutableMapping, Optional, Protocol

try:  # pragma: no cover - optional dependency handled lazily
    import cyndilib
except ImportError:  # pragma: no cover - exercised in tests via dependency injection
    cyndilib = None  # type: ignore

try:  # pragma: no cover - optional during unit tests
    from yup_rive_renderer import RiveOffscreenRenderer
except ImportError:  # pragma: no cover - exercised in tests via dependency injection
    RiveOffscreenRenderer = None  # type: ignore


_logger = logging.getLogger(__name__)


TimestampMapper = Callable[[float], int]
TimeProvider = Callable[[], float]


class SenderHandle(Protocol):
    """Protocol implemented by sender adapters used by :class:`NDIOrchestrator`."""

    def send (self, buffer: memoryview, width: int, height: int, stride: int, timestamp: int) -> None: ...

    def apply_metadata (self, metadata: Mapping[str, Mapping[str, Any]]) -> None: ...

    def close (self) -> None: ...


class RendererFactory(Protocol):
    """Factory protocol used to instantiate renderers for each stream."""

    def __call__ (self, config: "NDIStreamConfig") -> Any: ...


class SenderFactory(Protocol):
    """Factory protocol used to create sender handles for each stream."""

    def __call__ (self, config: "NDIStreamConfig", renderer: Any) -> SenderHandle: ...


ControlHandler = Callable[["NDIOrchestrator", str, Mapping[str, Any]], Any]


@dataclass(slots=True)
class NDIStreamConfig:
    """Configuration payload for registering a renderer/NDI publishing pipeline."""

    name: str
    width: int
    height: int
    riv_path: Optional[str] = None
    riv_bytes: Optional[bytes] = None
    artboard: Optional[str] = None
    animation: Optional[str] = None
    state_machine: Optional[str] = None
    loop_animation: bool = True
    frame_rate: Optional[Fraction | float | tuple[int, int]] = None
    ndi_groups: str = ""
    clock_video: bool = True
    clock_audio: bool = False
    sender_options: MutableMapping[str, Any] = field(default_factory=dict)
    renderer_options: MutableMapping[str, Any] = field(default_factory=dict)
    metadata: MutableMapping[str, Mapping[str, Any]] = field(default_factory=dict)
    use_async_send: bool = True

    def __post_init__ (self) -> None:
        if not self.riv_path and self.riv_bytes is None:
            raise ValueError("Either 'riv_path' or 'riv_bytes' must be provided")

        if self.riv_path and self.riv_bytes is not None:
            _logger.debug("Stream %s supplied both riv_path and riv_bytes; riv_path will be used", self.name)
            self.riv_bytes = None

        if self.width <= 0 or self.height <= 0:
            raise ValueError("Renderer dimensions must be positive")

        if isinstance(self.frame_rate, tuple):
            self.frame_rate = Fraction(self.frame_rate[0], self.frame_rate[1])
        elif isinstance(self.frame_rate, float):
            self.frame_rate = Fraction(self.frame_rate).limit_denominator()
        elif self.frame_rate is not None and not isinstance(self.frame_rate, Fraction):
            raise TypeError("frame_rate must be a Fraction, float, tuple, or None")


class _NDIStream:
    """Internal book-keeping for a single renderer + sender pair."""

    def __init__ (
        self,
        config: NDIStreamConfig,
        renderer: Any,
        sender_handle: SenderHandle,
        timestamp_mapper: TimestampMapper,
        time_provider: TimeProvider,
    ) -> None:
        self.config = config
        self.renderer = renderer
        self.sender_handle = sender_handle
        self._timestamp_mapper = timestamp_mapper
        self._time_provider = time_provider

    def advance_and_send (self, delta_seconds: float, timestamp: Optional[float]) -> bool:
        now = timestamp if timestamp is not None else self._time_provider()
        ndi_timestamp = self._timestamp_mapper(now)

        progressed = bool(self.renderer.advance(float(delta_seconds)))
        buffer = self._acquire_frame_buffer()
        self.sender_handle.send(buffer, self.renderer.get_width(), self.renderer.get_height(), self.renderer.get_row_stride(), ndi_timestamp)

        return progressed

    def _acquire_frame_buffer (self) -> memoryview:
        view_getter = getattr(self.renderer, "acquire_frame_view", None)

        if callable(view_getter):
            try:
                view = view_getter()
                flattened = memoryview(view).cast("B")
                return flattened
            except (TypeError, ValueError):
                _logger.debug("Falling back to get_frame_bytes for stream %s", self.config.name)
            except Exception:  # pragma: no cover - diagnostic only
                _logger.exception("acquire_frame_view failed; falling back to get_frame_bytes")

        frame_bytes = self.renderer.get_frame_bytes()
        if isinstance(frame_bytes, memoryview):
            return frame_bytes.cast("B")
        if isinstance(frame_bytes, (bytes, bytearray)):  # zero-copy read-only in CPython
            return memoryview(frame_bytes)

        return memoryview(bytes(frame_bytes))

    def apply_control (self, action: str, parameters: Mapping[str, Any]) -> Any:
        action = action.lower()
        if action == "pause":
            self.renderer.set_paused(True)
            return True
        if action == "resume":
            self.renderer.set_paused(False)
            return True
        if action == "play_animation":
            name = parameters.get("name")
            if not name:
                raise ValueError("play_animation requires a 'name' parameter")
            loop = bool(parameters.get("loop", True))
            return self.renderer.play_animation(name, loop)
        if action == "play_state_machine":
            name = parameters.get("name")
            if not name:
                raise ValueError("play_state_machine requires a 'name' parameter")
            return self.renderer.play_state_machine(name)
        if action == "stop":
            self.renderer.stop()
            return True
        if action == "select_artboard":
            name = parameters.get("name")
            if not name:
                raise ValueError("select_artboard requires a 'name' parameter")
            self.renderer.select_artboard(name)
            return True
        if action == "set_input":
            input_name = parameters.get("name")
            if not input_name:
                raise ValueError("set_input requires a 'name' parameter")
            value = parameters.get("value")
            if isinstance(value, bool):
                return self.renderer.set_bool_input(input_name, value)
            if isinstance(value, (int, float)):
                return self.renderer.set_number_input(input_name, float(value))
            if value == "trigger":
                return self.renderer.fire_trigger(input_name)
            raise ValueError("Unsupported input value type")

        raise ValueError(f"Unknown control action: {action}")

    def close (self) -> None:
        try:
            self.sender_handle.close()
        finally:
            stop = getattr(self.renderer, "stop", None)
            if callable(stop):
                stop()


def _default_timestamp_mapper (seconds: float) -> int:
    return int(seconds * 10_000_000)


def _default_time_provider () -> float:
    return time.perf_counter()


def _default_renderer_factory (config: NDIStreamConfig) -> Any:
    if RiveOffscreenRenderer is None:  # pragma: no cover - executed only in production without injection
        raise ImportError("yup_rive_renderer is not available; build the extension before creating streams")

    if config.renderer_options:
        _logger.warning("Renderer options are ignored by the default factory")

    renderer = RiveOffscreenRenderer(config.width, config.height)
    if not renderer.is_valid():
        raise RuntimeError("Failed to initialise RiveOffscreenRenderer")

    return renderer


class _CyndiLibSenderHandle:
    """Concrete :class:`SenderHandle` that wraps :mod:`cyndilib` senders."""

    def __init__ (self, sender: Any, video_frame: Any, use_async: bool) -> None:
        self._sender = sender
        self._video_frame = video_frame
        self._use_async = use_async

    def send (self, buffer: memoryview, width: int, height: int, stride: int, timestamp: int) -> None:
        try:
            self._video_frame.set_resolution(width, height)
        except Exception:  # pragma: no cover - defensive, API documented as safe
            pass

        try:
            setter = getattr(self._video_frame, "_set_line_stride", None)
            if callable(setter):
                setter(stride)
        except Exception:  # pragma: no cover - defensive
            _logger.debug("Failed to apply explicit line stride; continuing")

        try:
            if hasattr(self._video_frame, "ptr") and hasattr(self._video_frame.ptr, "timestamp"):
                self._video_frame.ptr.timestamp = timestamp
        except Exception:  # pragma: no cover - best effort only
            _logger.debug("Unable to set NDI timestamp on video frame")

        contiguous = buffer.cast("B")
        self._sender.write_video(contiguous)
        if self._use_async:
            self._sender.send_video_async()
        else:
            self._sender.send_video()

    def apply_metadata (self, metadata: Mapping[str, Mapping[str, Any]]) -> None:
        if not metadata:
            return

        if not hasattr(self._sender, "send_metadata"):
            _logger.warning("Sender implementation does not expose send_metadata")
            return

        for tag, attrs in metadata.items():
            try:
                self._sender.send_metadata(tag, dict(attrs))
            except Exception:  # pragma: no cover - metadata transmission is best-effort
                _logger.exception("Failed to send metadata '%s'", tag)

    def close (self) -> None:
        try:
            self._sender.close()
        except Exception:  # pragma: no cover - finalisation should not raise
            _logger.debug("NDI sender close failed", exc_info=True)


def _default_sender_factory (config: NDIStreamConfig, renderer: Any) -> SenderHandle:
    if cyndilib is None:  # pragma: no cover - executed only in production without injection
        raise ImportError("cyndilib is not installed; install cyndilib>=0.0.8 to stream over NDI")

    sender_kwargs = dict(config.sender_options)
    sender_kwargs.setdefault("clock_video", config.clock_video)
    sender_kwargs.setdefault("clock_audio", config.clock_audio)

    sender = cyndilib.Sender(config.name, ndi_groups=config.ndi_groups, **sender_kwargs)

    video_frame = cyndilib.VideoSendFrame()
    video_frame.set_fourcc(cyndilib.FourCC.BGRA)
    video_frame.set_resolution(renderer.get_width(), renderer.get_height())

    if config.frame_rate is not None:
        video_frame.set_frame_rate(config.frame_rate)

    sender.set_video_frame(video_frame)
    sender.open()

    return _CyndiLibSenderHandle(sender, video_frame, config.use_async_send)


class NDIOrchestrator:
    """Coordinates Rive renderer instances and NDI senders."""

    def __init__ (
        self,
        renderer_factory: Optional[RendererFactory] = None,
        sender_factory: Optional[SenderFactory] = None,
        timestamp_mapper: TimestampMapper = _default_timestamp_mapper,
        time_provider: TimeProvider = _default_time_provider,
    ) -> None:
        self._renderer_factory = renderer_factory or _default_renderer_factory
        self._sender_factory = sender_factory or _default_sender_factory
        self._timestamp_mapper = timestamp_mapper
        self._time_provider = time_provider
        self._streams: Dict[str, _NDIStream] = {}
        self._control_handlers: Dict[str, ControlHandler] = {}

    def __enter__ (self) -> "NDIOrchestrator":
        return self

    def __exit__ (self, exc_type, exc, tb) -> None:
        self.close()

    def add_stream (self, config: NDIStreamConfig) -> None:
        if config.name in self._streams:
            raise ValueError(f"A stream named '{config.name}' already exists")

        renderer = self._renderer_factory(config)
        self._initialise_renderer(renderer, config)

        sender_handle = self._sender_factory(config, renderer)
        stream = _NDIStream(config, renderer, sender_handle, self._timestamp_mapper, self._time_provider)
        self._streams[config.name] = stream

        sender_handle.apply_metadata(config.metadata)

    def remove_stream (self, name: str) -> None:
        stream = self._streams.pop(name)
        stream.close()

    def list_streams (self) -> List[str]:
        return list(self._streams.keys())

    def advance_stream (self, name: str, delta_seconds: float, timestamp: Optional[float] = None) -> bool:
        stream = self._streams[name]
        return stream.advance_and_send(delta_seconds, timestamp)

    def advance_all (self, delta_seconds: float, timestamp: Optional[float] = None) -> Dict[str, bool]:
        results: Dict[str, bool] = {}
        for name, stream in list(self._streams.items()):
            results[name] = stream.advance_and_send(delta_seconds, timestamp)
        return results

    def apply_stream_control (self, name: str, action: str, parameters: Optional[Mapping[str, Any]] = None) -> Any:
        stream = self._streams[name]
        return stream.apply_control(action, parameters or {})

    def register_control_handler (self, command: str, handler: ControlHandler) -> None:
        self._control_handlers[command] = handler

    def unregister_control_handler (self, command: str) -> None:
        self._control_handlers.pop(command, None)

    def dispatch_control (self, command: str, payload: Optional[Mapping[str, Any]] = None) -> Any:
        handler = self._control_handlers.get(command)
        if handler is None:
            raise KeyError(f"No handler registered for '{command}'")
        return handler(self, command, payload or {})

    def close (self) -> None:
        for name in list(self._streams.keys()):
            self.remove_stream(name)

    def _initialise_renderer (self, renderer: Any, config: NDIStreamConfig) -> None:
        if config.riv_path:
            renderer.load_file(config.riv_path, config.artboard)
        elif config.riv_bytes is not None:
            renderer.load_bytes(config.riv_bytes, config.artboard)
        else:  # pragma: no cover - guarded earlier but defensive
            raise ValueError("Stream config missing riv_path/riv_bytes")

        if config.animation:
            ok = renderer.play_animation(config.animation, config.loop_animation)
            if not ok:
                raise RuntimeError(f"Failed to start animation '{config.animation}'")

        if config.state_machine:
            ok = renderer.play_state_machine(config.state_machine)
            if not ok:
                raise RuntimeError(f"Failed to start state machine '{config.state_machine}'")

        if hasattr(renderer, "set_paused"):
            renderer.set_paused(False)
