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
import sys
import time
from pathlib import Path

import numpy as np


DEFAULT_RIV = Path(__file__).resolve().parents[1] / "examples" / "graphics" / "data" / "charge.riv"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Benchmark yup_rive_renderer throughput and zero-copy behaviour.")
    parser.add_argument("--frames", type=int, default=1000, help="Number of frames to render (default: %(default)s)")
    parser.add_argument("--width", type=int, default=1920, help="Renderer width in pixels (default: %(default)s)")
    parser.add_argument("--height", type=int, default=1080, help="Renderer height in pixels (default: %(default)s)")
    parser.add_argument("--riv", type=Path, default=DEFAULT_RIV, help="Path to the Rive file to load (default: %(default)s)")
    parser.add_argument("--delta", type=float, default=1.0 / 60.0, help="Delta time passed to advance() per frame (default: %(default)s)")
    return parser.parse_args()


def load_renderer(width: int, height: int, riv_path: Path):
    try:
        from yup_rive_renderer import RiveOffscreenRenderer
    except ImportError as exc:  # pragma: no cover - exercised manually
        print("[benchmark_renderer] ERROR: yup_rive_renderer extension is not available", file=sys.stderr)
        raise SystemExit(1) from exc

    renderer = RiveOffscreenRenderer(width, height)
    if not renderer.is_valid():  # pragma: no cover - depends on environment
        print("[benchmark_renderer] ERROR: RiveOffscreenRenderer backend is not initialised", file=sys.stderr)
        raise SystemExit(1)

    renderer.load_file(str(riv_path))
    return renderer


def benchmark(renderer, frames: int, delta: float) -> tuple[float, bool]:
    addresses: set[int] = set()
    start = time.perf_counter()

    for _ in range(frames):
        renderer.advance(delta)
        view = renderer.acquire_frame_view()
        flat = memoryview(view).cast("B")
        array = np.frombuffer(flat, dtype=np.uint8)
        addresses.add(int(array.__array_interface__["data"][0]))

    elapsed = time.perf_counter() - start
    zero_copy = len(addresses) == 1
    return elapsed, zero_copy


def main() -> int:
    args = parse_args()
    riv_path = args.riv

    if not riv_path.exists():
        print(f"[benchmark_renderer] ERROR: Rive file not found: {riv_path}", file=sys.stderr)
        return 1

    renderer = load_renderer(args.width, args.height, riv_path)

    elapsed, zero_copy = benchmark(renderer, args.frames, args.delta)
    fps = args.frames / elapsed if elapsed > 0 else float("inf")

    print(f"Frames rendered: {args.frames}")
    print(f"Total time: {elapsed:.3f} s")
    print(f"Average FPS: {fps:.2f}")
    print(f"Zero-Copy: {'PASSED' if zero_copy else 'FAILED'}")

    return 0 if zero_copy else 1


if __name__ == "__main__":
    sys.exit(main())
