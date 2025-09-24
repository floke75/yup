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

import argparse
import logging
import signal
import sys
import threading
import time
from typing import Any, Dict, Mapping, MutableMapping, Optional, Sequence

from .orchestrator import NDIOrchestrator, NDIStreamConfig

__all__ = ["main"]

_logger = logging.getLogger(__name__)


def _build_parser () -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Stream Rive animations to NDI using yup_ndi")
    parser.add_argument("--name", required=True, help="NDI source name to publish")
    parser.add_argument("--riv-file", required=True, help="Path to the .riv file to load")
    parser.add_argument("--width", type=int, required=True, help="Renderer width in pixels")
    parser.add_argument("--height", type=int, required=True, help="Renderer height in pixels")
    parser.add_argument("--artboard", help="Optional artboard to select after loading the file")
    parser.add_argument("--animation", help="Optional animation to play immediately")
    parser.add_argument("--state-machine", help="Optional state machine to play immediately")
    parser.add_argument("--no-loop", action="store_true", help="Disable looping when --animation is provided")
    parser.add_argument("--fps", type=float, default=60.0, help="Frame rate to target; set to 0 to use wall-clock deltas")
    parser.add_argument("--ndi-groups", default="", help="Comma-separated NDI group names")
    parser.add_argument("--no-clock-video", dest="clock_video", action="store_false", default=True, help="Disable NDI video clocking")
    parser.add_argument("--clock-audio", action="store_true", help="Enable NDI audio clocking")
    parser.add_argument("--sender-option", action="append", default=[], metavar="KEY=VALUE", help="Additional cyndilib Sender options")
    parser.add_argument("--metadata", action="append", default=[], metavar="KEY=VALUE", help="Metadata key/value pairs published under the 'ndi' tag")
    parser.add_argument("--state-input", action="append", default=[], metavar="NAME=VALUE", help="Initial state machine input values")
    parser.add_argument("--sync-send", dest="use_async_send", action="store_false", default=True, help="Send frames synchronously instead of using cyndilib's async queue")
    parser.add_argument("--no-throttle", dest="throttle_when_inactive", action="store_false", default=True, help="Continue rendering even when no NDI receivers are connected")
    parser.add_argument("--pause-when-inactive", action="store_true", help="Pause the renderer when no NDI receivers are connected")
    parser.add_argument("--poll-interval", type=float, default=1.0, help="Seconds between connection-count polls when throttling is enabled")
    parser.add_argument("--rest-host", default="127.0.0.1", help="Host interface for the optional REST control server")
    parser.add_argument("--rest-port", type=int, help="Port for the optional REST control server")
    parser.add_argument("--osc-host", default="127.0.0.1", help="Host interface for the optional OSC control server")
    parser.add_argument("--osc-port", type=int, help="Port for the optional OSC control server")
    parser.add_argument("--osc-namespace", default="/ndi", help="OSC namespace prefix (default: /ndi)")
    parser.add_argument("--log-level", default="INFO", help="Python logging level (default: INFO)")
    parser.add_argument("--metrics-interval", type=float, default=10.0, help="Seconds between periodic metrics logs; set to 0 to disable")
    return parser


def _parse_key_value_pairs (pairs: Sequence[str], option: str) -> Dict[str, str]:
    result: Dict[str, str] = {}
    for pair in pairs:
        if "=" not in pair:
            raise ValueError(f"{option} expects KEY=VALUE entries; received '{pair}'")
        key, value = pair.split("=", 1)
        result[key.strip()] = value.strip()
    return result


def _parse_state_inputs (pairs: Sequence[str]) -> MutableMapping[str, Any]:
    inputs: MutableMapping[str, Any] = {}
    for pair in pairs:
        if "=" not in pair:
            raise ValueError(f"--state-input expects NAME=VALUE entries; received '{pair}'")
        name, value = pair.split("=", 1)
        inputs[name.strip()] = _coerce_scalar(value.strip())
    return inputs


def _coerce_scalar (value: str) -> Any:
    lowered = value.lower()
    if lowered in {"true", "false"}:
        return lowered == "true"

    try:
        return int(value)
    except ValueError:
        try:
            return float(value)
        except ValueError:
            return value


class _FramePump:
    def __init__ (
        self,
        orchestrator: NDIOrchestrator,
        stream_name: str,
        target_fps: float,
        metrics_interval: float,
    ) -> None:
        self._orchestrator = orchestrator
        self._stream_name = stream_name
        self._delta = 1.0 / target_fps if target_fps > 0 else None
        self._metrics_interval = metrics_interval
        self._stop_event = threading.Event()

    def stop (self) -> None:
        self._stop_event.set()

    def run (self) -> None:
        previous_time = time.perf_counter()
        last_metrics = time.monotonic()

        def _handle_signal (signum: int, frame: Optional[object]) -> None:  # pragma: no cover - signal handler
            _logger.info("Received signal %s; stopping", signum)
            self._stop_event.set()

        original_int = signal.getsignal(signal.SIGINT)
        original_term = signal.getsignal(signal.SIGTERM)
        signal.signal(signal.SIGINT, _handle_signal)
        signal.signal(signal.SIGTERM, _handle_signal)

        try:
            while not self._stop_event.is_set() and self._stream_name in self._orchestrator.list_streams():
                now = time.perf_counter()
                delta = self._delta if self._delta is not None else max(0.0, now - previous_time)
                timestamp = now
                try:
                    self._orchestrator.advance_stream(self._stream_name, delta, timestamp)
                except KeyError:
                    _logger.error("Stream '%s' is no longer registered", self._stream_name)
                    break

                previous_time = now if self._delta is None else previous_time + self._delta

                if self._metrics_interval > 0 and (time.monotonic() - last_metrics) >= self._metrics_interval:
                    metrics = self._orchestrator.get_stream_metrics(self._stream_name)
                    _logger.info(
                        "Stream %s → sent=%d suppressed=%d connections=%d paused=%s",
                        self._stream_name,
                        metrics.frames_sent,
                        metrics.frames_suppressed,
                        metrics.last_connection_count,
                        metrics.paused_for_inactivity,
                    )
                    last_metrics = time.monotonic()

                if self._delta is not None:
                    elapsed = time.perf_counter() - now
                    sleep_time = self._delta - elapsed
                    if sleep_time > 0:
                        time.sleep(sleep_time)
                else:
                    time.sleep(0)
        except KeyboardInterrupt:  # pragma: no cover - interactive guard
            _logger.info("Stopping due to keyboard interrupt")
        finally:
            signal.signal(signal.SIGINT, original_int)
            signal.signal(signal.SIGTERM, original_term)


def main (argv: Optional[Sequence[str]] = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)

    logging.basicConfig(level=getattr(logging, args.log_level.upper(), logging.INFO))

    try:
        sender_options = _parse_key_value_pairs(args.sender_option, "--sender-option")
        metadata_pairs = _parse_key_value_pairs(args.metadata, "--metadata")
        state_inputs = _parse_state_inputs(args.state_input)
    except ValueError as exc:
        parser.error(str(exc))

    metadata: MutableMapping[str, Mapping[str, Any]] = {}
    if metadata_pairs:
        metadata["ndi"] = metadata_pairs

    frame_rate: Optional[float] = args.fps if args.fps > 0 else None

    config = NDIStreamConfig(
        name=args.name,
        width=args.width,
        height=args.height,
        riv_path=args.riv_file,
        artboard=args.artboard,
        animation=args.animation,
        loop_animation=not args.no_loop,
        state_machine=args.state_machine,
        frame_rate=frame_rate,
        ndi_groups=args.ndi_groups,
        clock_video=args.clock_video,
        clock_audio=args.clock_audio,
        sender_options=dict(sender_options),
        metadata=metadata,
        state_machine_inputs=state_inputs,
        use_async_send=args.use_async_send,
        throttle_when_inactive=args.throttle_when_inactive,
        pause_when_inactive=args.pause_when_inactive,
        inactive_connection_poll_interval=max(0.0, args.poll_interval),
    )

    orchestrator = NDIOrchestrator()
    orchestrator.add_stream(config)

    active_servers: list[Any] = []

    if args.rest_port is not None:
        from .rest_server import start_rest_server

        active_servers.append(start_rest_server(orchestrator, host=args.rest_host, port=args.rest_port))

    if args.osc_port is not None:
        from .osc_server import start_osc_server

        active_servers.append(start_osc_server(orchestrator, host=args.osc_host, port=args.osc_port, namespace=args.osc_namespace))

    pump = _FramePump(orchestrator, args.name, args.fps, args.metrics_interval)

    try:
        pump.run()
    finally:
        pump.stop()
        for server in active_servers:
            try:
                server.close()
            except Exception:  # pragma: no cover - shutdown best effort
                _logger.exception("Failed to stop control server cleanly")
        orchestrator.close()

    return 0


if __name__ == "__main__":  # pragma: no cover - manual invocation
    sys.exit(main())
