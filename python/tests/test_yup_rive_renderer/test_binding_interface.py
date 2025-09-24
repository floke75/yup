from __future__ import annotations

from typing import Any

import pytest


yup_rive_renderer = pytest.importorskip(
    "yup_rive_renderer",
    reason="yup_rive_renderer extension is not available",
)


@pytest.fixture()
def renderer() -> Any:
    instance = yup_rive_renderer.RiveOffscreenRenderer(8, 8)
    if not instance.is_valid():
        pytest.skip("RiveOffscreenRenderer backend is not initialised")
    return instance


def _ensure_frame_view(renderer: Any) -> memoryview:
    renderer.advance(0.0)
    view = renderer.acquire_frame_view()
    if view.nbytes == 0:
        pytest.skip("Renderer returned an empty frame buffer")
    return view


def test_renderer_construction_reports_dimensions(renderer: Any) -> None:
    assert renderer.get_width() == 8
    assert renderer.get_height() == 8

    stride = renderer.get_row_stride()
    if stride <= 0:
        pytest.skip("Renderer did not expose a valid row stride")

    assert stride >= renderer.get_width() * 4


def test_acquire_frame_view_matches_frame_bytes(renderer: Any) -> None:
    view = _ensure_frame_view(renderer)

    assert isinstance(view, memoryview)
    assert view.readonly is True
    assert view.itemsize == 1

    if view.ndim >= 3:
        assert view.shape[-1] == 4

    flat = view.cast("B")
    assert flat.format == "B"
    assert flat.readonly is True
    assert flat.obj is not None

    frame_bytes = renderer.get_frame_bytes()
    assert isinstance(frame_bytes, (bytes, bytearray))
    assert flat.tobytes() == bytes(frame_bytes)


def test_constructor_accepts_staging_buffer_count () -> None:
    instance = yup_rive_renderer.RiveOffscreenRenderer(4, 4, staging_buffer_count=3)
    assert instance.get_width() == 4
    assert instance.get_height() == 4
