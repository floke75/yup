/*
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
*/

#include <yup_gui/yup_gui.h>

#include <cstddef>

#include <gtest/gtest.h>

using namespace yup;

namespace
{
constexpr int kWidth = 64;
constexpr int kHeight = 32;
constexpr std::size_t kExpectedStride = static_cast<std::size_t> (kWidth) * 4u;
constexpr std::size_t kExpectedBufferSize = static_cast<std::size_t> (kWidth) * static_cast<std::size_t> (kHeight) * 4u;
} // namespace

TEST (RiveOffscreenRendererTest, ReportsDimensionsAndFrameLayout)
{
    RiveOffscreenRenderer renderer (kWidth, kHeight);

    EXPECT_EQ (kWidth, renderer.getWidth());
    EXPECT_EQ (kHeight, renderer.getHeight());
    EXPECT_EQ (kExpectedStride, renderer.getRowStride());

    const auto& frameBuffer = renderer.getFrameBuffer();
    EXPECT_EQ (kExpectedBufferSize, frameBuffer.size());

    auto sharedBuffer = renderer.getFrameBufferShared();
    ASSERT_TRUE (sharedBuffer);
    EXPECT_EQ (kExpectedBufferSize, sharedBuffer->size());
    EXPECT_EQ (sharedBuffer.get(), &frameBuffer);
}

TEST (RiveOffscreenRendererTest, SharedBufferReflectsLatestFrame)
{
    RiveOffscreenRenderer renderer (kWidth, kHeight);

    auto firstView = renderer.getFrameBufferShared();
    ASSERT_TRUE (firstView);

    const auto* initialData = firstView->data();

    const auto& frameBuffer = renderer.getFrameBuffer();
    EXPECT_EQ (initialData, frameBuffer.data());

    auto secondView = renderer.getFrameBufferShared();
    EXPECT_EQ (initialData, secondView->data());
    EXPECT_EQ (firstView.get(), secondView.get());
}

TEST (RiveOffscreenRendererTest, PauseStateCanBeToggled)
{
    RiveOffscreenRenderer renderer (kWidth, kHeight);

    EXPECT_FALSE (renderer.isPaused());

    renderer.setPaused (true);
    EXPECT_TRUE (renderer.isPaused());

    renderer.stop();
    EXPECT_FALSE (renderer.isPaused());
}

