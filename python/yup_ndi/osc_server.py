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

import json
import logging
import threading
from typing import Any, Dict, Mapping, Sequence, Tuple

from .orchestrator import NDIOrchestrator

__all__ = ["OscControlServer", "start_osc_server"]

_logger = logging.getLogger(__name__)


class OscControlServer:
    """Expose :class:`NDIOrchestrator` controls via Open Sound Control messages.

    The server mirrors the orchestrator's control surface and replies to
    ``/<namespace>/<stream>/metrics`` requests with a JSON payload summarising
    recent frame counts and connection details so monitoring clients receive
    immediate feedback.
    """

    def __init__ (
        self,
        orchestrator: NDIOrchestrator,
        host: str = "127.0.0.1",
        port: int = 5001,
        namespace: str = "/ndi",
    ) -> None:
        try:  # pragma: no cover - exercised when python-osc is installed
            from pythonosc.dispatcher import Dispatcher
            from pythonosc.osc_server import ThreadingOSCUDPServer
            from pythonosc.udp_client import SimpleUDPClient
        except ImportError as exc:  # pragma: no cover - import guard
            raise ImportError("python-osc is required for the OSC control server; install python-osc>=1.8") from exc

        self._orchestrator = orchestrator
        self._host = host
        self._port = port
        base_namespace = namespace.strip("/")
        self._namespace = f"/{base_namespace}" if base_namespace else "/ndi"
        self._dispatcher = Dispatcher()
        self._dispatcher.map(f"{self._namespace}/*/control", self._handle_control)
        self._dispatcher.map(
            f"{self._namespace}/*/metrics",
            self._handle_metrics,
            needs_reply_address=True,
        )
        self._server_factory = ThreadingOSCUDPServer
        self._client_factory = SimpleUDPClient
        self._server: Any = None
        self._thread: threading.Thread | None = None
        self._client_cache: Dict[Tuple[str, int], Any] = {}

    def __enter__ (self) -> "OscControlServer":
        self.start()
        return self

    def __exit__ (self, exc_type, exc, tb) -> None:
        self.close()

    def start (self) -> None:
        if self._server is not None:
            return

        self._server = self._server_factory((self._host, self._port), self._dispatcher)
        self._server.daemon_threads = True
        self._thread = threading.Thread(target=self._server.serve_forever, name="yup-osc-control", daemon=True)
        self._thread.start()
        _logger.info("OSC control server listening on %s:%d%s", self._host, self._port, self._namespace)

    def close (self) -> None:
        if self._server is None:
            self._client_cache.clear()
            return

        _logger.info("Stopping OSC control server on %s:%d%s", self._host, self._port, self._namespace)
        self._server.shutdown()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
        self._server = None
        self._thread = None
        self._client_cache.clear()

    def _handle_control (self, address: str, *args: Any) -> None:
        stream_name = self._extract_stream_name(address)
        if stream_name is None:
            _logger.warning("Ignoring OSC control for malformed address '%s'", address)
            return

        if not args:
            _logger.warning("OSC control for '%s' missing action argument", stream_name)
            return

        action = str(args[0])
        parameters = self._coerce_parameters(args[1:])

        try:
            self._orchestrator.apply_stream_control(stream_name, action, parameters)
        except KeyError:
            _logger.warning("OSC control target stream '%s' not found", stream_name)
        except Exception:  # pragma: no cover - runtime guard
            _logger.exception("OSC control failed for stream '%s'", stream_name)

    def _handle_metrics (self, client_address: Tuple[str, int], address: str, *args: Any) -> None:
        stream_name = self._extract_stream_name(address)
        if stream_name is None:
            _logger.warning("Ignoring OSC metrics request for malformed address '%s'", address)
            return

        try:
            metrics = self._orchestrator.get_stream_metrics(stream_name)
        except KeyError:
            _logger.warning("OSC metrics target stream '%s' not found", stream_name)
            return

        payload = {
            "frames_sent": metrics.frames_sent,
            "frames_suppressed": metrics.frames_suppressed,
            "connections": metrics.last_connection_count,
            "paused": metrics.paused_for_inactivity,
        }
        _logger.info("OSC metrics for %s: %s", stream_name, payload)

        try:
            host, port = client_address
        except Exception:  # pragma: no cover - defensive parsing guard
            _logger.debug("OSC metrics reply address malformed: %r", client_address)
            return

        cache_key = (str(host), int(port))
        client = self._client_cache.get(cache_key)
        if client is None:
            try:
                client = self._client_factory(cache_key[0], cache_key[1])
            except Exception:  # pragma: no cover - client construction best effort
                _logger.exception("Failed to create OSC reply client for %s", cache_key)
                return
            self._client_cache[cache_key] = client

        message_path = f"{self._namespace}/{stream_name}/metrics"
        try:
            client.send_message(message_path, json.dumps(payload))
        except Exception:  # pragma: no cover - best effort reply
            _logger.exception("Failed to send OSC metrics response to %s", cache_key)

    def _extract_stream_name (self, address: str) -> str | None:
        parts = address.strip("/").split("/")
        if len(parts) < 3:
            return None
        if parts[0] != self._namespace.strip("/"):
            return None
        return parts[1]

    def _coerce_parameters (self, args: Sequence[Any]) -> Mapping[str, Any]:
        if not args:
            return {}

        first = args[0]
        if isinstance(first, Mapping):
            return dict(first)

        if isinstance(first, str):
            try:
                decoded = json.loads(first)
            except json.JSONDecodeError:
                decoded = None
            if isinstance(decoded, Mapping):
                return dict(decoded)
            if len(args) == 1:
                return {"value": first}

        if len(args) % 2 == 0:
            parameters: Dict[str, Any] = {}
            for index in range(0, len(args), 2):
                key = str(args[index])
                parameters[key] = args[index + 1]
            return parameters

        return {"value": first}

    def __repr__ (self) -> str:
        return f"OscControlServer(host={self._host!r}, port={self._port!r}, namespace={self._namespace!r})"


def start_osc_server (
    orchestrator: NDIOrchestrator,
    host: str = "127.0.0.1",
    port: int = 5001,
    namespace: str = "/ndi",
) -> OscControlServer:
    """Create and start an :class:`OscControlServer` instance."""

    server = OscControlServer(orchestrator, host=host, port=port, namespace=namespace)
    server.start()
    return server
