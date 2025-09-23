"""Smoke tests for the yup_rive_renderer pybind11 module."""
from __future__ import annotations

import pytest

binding = pytest.importorskip(
    "yup_rive_renderer", reason="yup_rive_renderer extension module is not available"
)


@pytest.mark.parametrize("width,height", [(16, 16), (1, 1)])
def test_renderer_construction_exposes_expected_accessors (width: int, height: int) -> None:
    renderer = binding.RiveOffscreenRenderer(width, height)

    assert hasattr(renderer, "is_valid")
    assert hasattr(renderer, "get_frame_bytes")
    assert hasattr(renderer, "acquire_frame_view")
    assert renderer.get_width() == width
    assert renderer.get_height() == height
    assert renderer.get_row_stride() >= width * 4


def test_renderer_acquire_frame_view_returns_memoryview () -> None:
    renderer = binding.RiveOffscreenRenderer(8, 8)

    if not renderer.is_valid():
        pytest.skip("Renderer could not initialise in this environment")

    view = renderer.acquire_frame_view()
    assert isinstance(view, memoryview)
    assert view.format == "B"

    frame_bytes = renderer.get_frame_bytes()
    assert isinstance(frame_bytes, (bytes, bytearray))
