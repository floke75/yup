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

from fractions import Fraction

import pytest

import yup_ndi.cli as cli


def test_parse_frame_rate_fraction_string () -> None:
    fraction, cadence = cli._parse_frame_rate("60000/1001")
    assert fraction == Fraction(60000, 1001)
    assert cadence == pytest.approx(60000 / 1001)


def test_parse_frame_rate_decimal_string () -> None:
    fraction, cadence = cli._parse_frame_rate("59.94")
    assert fraction == Fraction(2997, 50)
    assert cadence == pytest.approx(59.94)


def test_parse_frame_rate_zero_disables () -> None:
    fraction, cadence = cli._parse_frame_rate("0")
    assert fraction is None
    assert cadence is None


def test_parse_frame_rate_rejects_invalid_input () -> None:
    with pytest.raises(ValueError):
        cli._parse_frame_rate("not-a-number")


def test_parse_frame_rate_accepts_fraction_instance () -> None:
    rate = Fraction(24, 1)
    fraction, cadence = cli._parse_frame_rate(rate)
    assert fraction == rate
    assert cadence == pytest.approx(24.0)
