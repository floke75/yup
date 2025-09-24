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
from pathlib import Path
from typing import List, Mapping

import pytest

np = pytest.importorskip(
    "numpy",
    reason="NumPy is required for integration flow tests",
)


yup_rive_renderer = pytest.importorskip(
    "yup_rive_renderer",
    reason="yup_rive_renderer extension is not available",
)

from yup_ndi.orchestrator import NDIOrchestrator, NDIStreamConfig


@dataclass
class RecordedFrame:
    width: int
    height: int
    stride: int
    timestamp: int
    array: np.ndarray


class MockSenderHandle:
    def __init__(self) -> None:
        self.frames: List[RecordedFrame] = []
        self.metadata: List[Mapping[str, Mapping[str, object]]] = []
        self.closed = False

    def write_video(self, buffer: memoryview, width: int, height: int, stride: int, timestamp: int) -> None:
        flat = memoryview(buffer).cast("B")
        frame_array = np.frombuffer(flat, dtype=np.uint8)
        expected_size = height * stride
        if frame_array.size != expected_size:
            pytest.skip("Renderer did not expose a dense frame buffer")

        if stride % 4 != 0:
            pytest.skip("Frame stride is not aligned to 4-byte pixels")

        row_pixels = stride // 4
        frame_view = np.ndarray(
            shape=(height, row_pixels, 4),
            dtype=np.uint8,
            buffer=flat,
            strides=(stride, 4, 1),
        )
        self.frames.append(RecordedFrame(width, height, stride, timestamp, frame_view))

    def send(self, buffer: memoryview, width: int, height: int, stride: int, timestamp: int) -> None:
        self.write_video(buffer, width, height, stride, timestamp)

    def apply_metadata(self, metadata: Mapping[str, Mapping[str, object]]) -> None:
        if metadata:
            self.metadata.append(metadata)

    def close(self) -> None:
        self.closed = True


def _build_renderer(width: int, height: int) -> "yup_rive_renderer.RiveOffscreenRenderer":
    renderer = yup_rive_renderer.RiveOffscreenRenderer(width, height)
    if not renderer.is_valid():
        pytest.skip("RiveOffscreenRenderer backend is not initialised")
    return renderer


def test_renderer_to_orchestrator_frame_flow() -> None:
    width = 256
    height = 256
    renderer = _build_renderer(width, height)

    riv_path = Path(__file__).resolve().parents[3] / "examples" / "graphics" / "data" / "charge.riv"
    if not riv_path.exists():
        pytest.skip("Example Rive asset is missing")

    sender_handle = MockSenderHandle()

    def renderer_factory(config: NDIStreamConfig) -> "yup_rive_renderer.RiveOffscreenRenderer":
        return renderer

    def sender_factory(config: NDIStreamConfig, renderer_instance: object) -> MockSenderHandle:
        return sender_handle

    orchestrator = NDIOrchestrator(
        renderer_factory=renderer_factory,
        sender_factory=sender_factory,
        timestamp_mapper=lambda seconds: int(seconds * 1_000_000),
        time_provider=lambda: 0.0,
    )

    config = NDIStreamConfig(
        name="integration",
        width=width,
        height=height,
        riv_path=str(riv_path),
        loop_animation=True,
    )
    orchestrator.add_stream(config)

    frame_count = 3
    for index in range(frame_count):
        progressed = orchestrator.advance_stream("integration", 1.0 / 60.0, timestamp=float(index) / 60.0)
        assert progressed is True

    assert len(sender_handle.frames) == frame_count

    for recorded in sender_handle.frames:
        assert recorded.width == width
        assert recorded.height == height
        assert recorded.array.dtype == np.uint8
        assert recorded.array.shape[0] == height
        assert recorded.array.shape[2] == 4
        assert recorded.array.shape[1] >= width

    orchestrator.close()
    assert sender_handle.closed is True
