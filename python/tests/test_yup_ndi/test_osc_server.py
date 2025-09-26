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

import importlib
import json
import sys
import types
from dataclasses import dataclass
from typing import Any, Dict, Tuple

import pytest


@dataclass
class _FakeMetrics:
    frames_sent: int = 5
    frames_suppressed: int = 1
    last_connection_count: int = 2
    paused_for_inactivity: bool = False


class _FakeOrchestrator:
    def __init__ (self) -> None:
        self.metrics = _FakeMetrics()

    def get_stream_metrics (self, name: str) -> _FakeMetrics:
        if name != "demo":
            raise KeyError(name)
        return self.metrics


class _FakeDispatcher:
    def __init__ (self) -> None:
        self.records: list[Dict[str, Any]] = []

    def map (
        self,
        address: str,
        handler: Any,
        *args: Any,
        needs_reply_address: bool = False,
    ) -> Dict[str, Any]:
        record = {
            "address": address,
            "handler": handler,
            "args": args,
            "needs_reply_address": needs_reply_address,
        }
        self.records.append(record)
        return record


class _FakeThreadingOSCUDPServer:
    def __init__ (self, bind: Tuple[str, int], dispatcher: _FakeDispatcher) -> None:
        self.bind = bind
        self.dispatcher = dispatcher
        self.daemon_threads = False
        self._shutdown = False

    def serve_forever (self) -> None:  # pragma: no cover - not exercised
        pass

    def shutdown (self) -> None:
        self._shutdown = True


class _FakeSimpleUDPClient:
    def __init__ (self, host: str, port: int) -> None:
        self.host = host
        self.port = port
        self.messages: list[Tuple[str, Any]] = []

    def send_message (self, address: str, payload: Any) -> None:
        self.messages.append((address, payload))


def _install_pythonosc_stubs (monkeypatch: pytest.MonkeyPatch) -> None:
    package = types.ModuleType("pythonosc")
    package.__path__ = []  # type: ignore[attr-defined]

    dispatcher_module = types.ModuleType("pythonosc.dispatcher")
    dispatcher_module.Dispatcher = _FakeDispatcher

    osc_server_module = types.ModuleType("pythonosc.osc_server")
    osc_server_module.ThreadingOSCUDPServer = _FakeThreadingOSCUDPServer

    udp_client_module = types.ModuleType("pythonosc.udp_client")
    udp_client_module.SimpleUDPClient = _FakeSimpleUDPClient

    monkeypatch.setitem(sys.modules, "pythonosc", package)
    monkeypatch.setitem(sys.modules, "pythonosc.dispatcher", dispatcher_module)
    monkeypatch.setitem(sys.modules, "pythonosc.osc_server", osc_server_module)
    monkeypatch.setitem(sys.modules, "pythonosc.udp_client", udp_client_module)


def test_metrics_requests_receive_json_response (monkeypatch: pytest.MonkeyPatch) -> None:
    sys.modules.pop("yup_ndi.osc_server", None)
    _install_pythonosc_stubs(monkeypatch)

    module = importlib.import_module("yup_ndi.osc_server")

    orchestrator = _FakeOrchestrator()
    server = module.OscControlServer(orchestrator, host="0.0.0.0", port=9000)

    dispatcher: _FakeDispatcher = server._dispatcher  # type: ignore[assignment]
    metrics_records = [record for record in dispatcher.records if record["address"].endswith("/metrics")]
    assert metrics_records, "Expected metrics route to be registered"
    metrics_handler = metrics_records[0]
    assert metrics_handler["needs_reply_address"] is True

    handler = metrics_handler["handler"]
    client_address = ("127.0.0.1", 5005)
    handler("/ndi/demo/metrics", client_address)

    cache_key = (client_address[0], client_address[1])
    assert cache_key in server._client_cache  # type: ignore[attr-defined]
    client: _FakeSimpleUDPClient = server._client_cache[cache_key]  # type: ignore[index]
    assert client.messages, "Expected OSC metrics response to be sent"

    message_path, payload = client.messages[-1]
    assert message_path == "/ndi/demo/metrics"
    parsed = json.loads(payload)
    assert parsed == {
        "frames_sent": orchestrator.metrics.frames_sent,
        "frames_suppressed": orchestrator.metrics.frames_suppressed,
        "connections": orchestrator.metrics.last_connection_count,
        "paused": orchestrator.metrics.paused_for_inactivity,
    }

    orchestrator.metrics.frames_sent = 8
    handler("/ndi/demo/metrics", client_address)
    assert len(server._client_cache) == 1  # type: ignore[arg-type]
    assert len(client.messages) == 2

    handler("/ndi/demo/metrics", "ignored", client_address)
    assert len(client.messages) == 3

    server.close()
    assert server._client_cache == {}  # type: ignore[attr-defined]
