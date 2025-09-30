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

import argparse
import time
from fractions import Fraction
from pathlib import Path

from yup_ndi import NDIOrchestrator, NDIStreamConfig


def _parse_frame_rate(value: str) -> Fraction | None:
    value = value.strip()
    if not value:
        return None
    if "/" in value:
        numerator, denominator = value.split("/", 1)
        return Fraction(int(numerator), int(denominator))
    fps = float(value)
    if fps <= 0:
        return None
    return Fraction(fps).limit_denominator()


def main() -> None:
    parser = argparse.ArgumentParser(description="Stream a Rive file to NDI using yup_ndi")
    parser.add_argument("riv", type=Path, help="Path to the .riv file to stream")
    parser.add_argument("--name", default="RiveNDI", help="NDI source name")
    parser.add_argument("--width", type=int, default=1920, help="Output width in pixels")
    parser.add_argument("--height", type=int, default=1080, help="Output height in pixels")
    parser.add_argument("--artboard", help="Artboard to load (defaults to file default)")
    parser.add_argument("--animation", help="Animation to trigger automatically")
    parser.add_argument("--state-machine", dest="state_machine", help="State machine to trigger automatically")
    parser.add_argument("--fps", default="60", help="Render cadence: float or fraction (e.g. 60000/1001)")
    parser.add_argument("--ndi-groups", default="", help="Optional NDI group membership")
    parser.add_argument("--present-preview", action="store_true", help="Mirror frames in a local window for troubleshooting")
    args = parser.parse_args()

    frame_rate = _parse_frame_rate(args.fps)

    config = NDIStreamConfig(
        name=args.name,
        width=args.width,
        height=args.height,
        riv_path=str(args.riv.resolve()),
        artboard=args.artboard,
        animation=args.animation,
        state_machine=args.state_machine,
        frame_rate=frame_rate,
        ndi_groups=args.ndi_groups,
        renderer_options={"enable_presentation": args.present_preview},
    )

    with NDIOrchestrator() as orchestrator:
        orchestrator.add_stream(config)
        delta = 1.0 / float(frame_rate) if frame_rate else 0.0
        try:
            while True:
                if delta > 0:
                    orchestrator.advance_stream(args.name, delta)
                    time.sleep(delta)
                else:
                    start = time.perf_counter()
                    orchestrator.advance_stream(args.name, 0.0)
                    elapsed = time.perf_counter() - start
                    if elapsed < 1 / 60:
                        time.sleep(max(0.0, (1 / 60) - elapsed))
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
