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

import dataclasses
import logging
import threading
from typing import Any, Mapping, Optional

from .orchestrator import NDIOrchestrator

__all__ = ["RestControlServer", "start_rest_server"]

_logger = logging.getLogger(__name__)


class RestControlServer:
    """Expose :class:`NDIOrchestrator` controls over a small Flask REST API."""

    def __init__ (self, orchestrator: NDIOrchestrator, host: str = "127.0.0.1", port: int = 5000) -> None:
        try:  # pragma: no cover - exercised when Flask is available
            from flask import Flask, jsonify, request
            from werkzeug.serving import make_server
        except ImportError as exc:  # pragma: no cover - import guard
            raise ImportError("Flask is required for the REST control server; install flask>=2.3") from exc

        self._orchestrator = orchestrator
        self._host = host
        self._port = port
        self._app = Flask(__name__)
        self._jsonify = jsonify
        self._request = request
        self._make_server = make_server
        self._server = None
        self._thread: Optional[threading.Thread] = None

        self._register_routes()

    def __enter__ (self) -> "RestControlServer":
        self.start()
        return self

    def __exit__ (self, exc_type, exc, tb) -> None:
        self.close()

    def start (self) -> None:
        if self._server is not None:
            return

        self._server = self._make_server(self._host, self._port, self._app)
        self._server.daemon_threads = True
        self._thread = threading.Thread(target=self._serve_forever, name="yup-rest-control", daemon=True)
        self._thread.start()
        _logger.info("REST control server listening on http://%s:%d", self._host, self._port)

    def close (self) -> None:
        if self._server is None:
            return

        _logger.info("Stopping REST control server on http://%s:%d", self._host, self._port)
        self._server.shutdown()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
        self._server = None
        self._thread = None

    def _serve_forever (self) -> None:
        assert self._server is not None
        try:
            self._server.serve_forever()
        except Exception:  # pragma: no cover - background server loop
            _logger.exception("REST control server terminated unexpectedly")

    def _register_routes (self) -> None:
        app = self._app

        @app.get("/health")
        def health () -> Any:
            return self._jsonify({"status": "ok"})

        @app.get("/streams")
        def list_streams () -> Any:
            payload = {
                name: dataclasses.asdict(self._orchestrator.get_stream_metrics(name))
                for name in self._orchestrator.list_streams()
            }
            return self._jsonify({"streams": payload})

        @app.get("/streams/<name>")
        def get_stream (name: str) -> Any:
            try:
                metrics = dataclasses.asdict(self._orchestrator.get_stream_metrics(name))
            except KeyError:
                return self._jsonify({"error": f"Stream '{name}' not found"}), 404
            return self._jsonify({"name": name, "metrics": metrics})

        @app.post("/streams/<name>/control")
        def control_stream (name: str) -> Any:
            payload = self._request.get_json(force=True, silent=True) or {}
            action = payload.get("action")
            if not action:
                return self._jsonify({"error": "Request body must include an 'action'"}), 400

            parameters = payload.get("parameters") or {}
            if not isinstance(parameters, Mapping):
                return self._jsonify({"error": "'parameters' must be a mapping"}), 400

            try:
                result = self._orchestrator.apply_stream_control(name, action, parameters)
            except KeyError:
                return self._jsonify({"error": f"Stream '{name}' not found"}), 404
            except Exception as exc:  # pragma: no cover - runtime guard
                _logger.exception("REST control failed for stream '%s'", name)
                return self._jsonify({"error": str(exc)}), 400

            return self._jsonify({"result": result})

        @app.post("/streams/<name>/metadata")
        def metadata_stream (name: str) -> Any:
            payload = self._request.get_json(force=True, silent=True)
            if not isinstance(payload, Mapping):
                return self._jsonify({"error": "Metadata payload must be a mapping"}), 400

            try:
                self._orchestrator.apply_stream_metadata(name, payload)  # type: ignore[arg-type]
            except KeyError:
                return self._jsonify({"error": f"Stream '{name}' not found"}), 404
            except Exception as exc:  # pragma: no cover - runtime guard
                _logger.exception("Failed to apply metadata for stream '%s'", name)
                return self._jsonify({"error": str(exc)}), 400

            return self._jsonify({"status": "ok"})

    def __repr__ (self) -> str:
        return f"RestControlServer(host={self._host!r}, port={self._port!r})"


def start_rest_server (orchestrator: NDIOrchestrator, host: str = "127.0.0.1", port: int = 5000) -> RestControlServer:
    """Create and start a :class:`RestControlServer` instance."""

    server = RestControlServer(orchestrator, host=host, port=port)
    server.start()
    return server
