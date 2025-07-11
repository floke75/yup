/*
  ==============================================================================

   This file is part of the YUP library.
   Copyright (c) 2024 - kunitoki@gmail.com

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

#include <gtest/gtest.h>

#include <yup_graphics/yup_graphics.h>

#include <cmath>
#include <tuple>

using namespace yup;

namespace
{
static constexpr float tol = 1e-5f;
} // namespace

TEST (ColorTests, Default_Constructor)
{
    Color c;
    EXPECT_EQ (c.getARGB(), 0xff000000u);
    EXPECT_EQ (c.getAlpha(), 255);
    EXPECT_EQ (c.getRed(), 0);
    EXPECT_EQ (c.getGreen(), 0);
    EXPECT_EQ (c.getBlue(), 0);
    EXPECT_TRUE (c.isOpaque());
    EXPECT_FALSE (c.isTransparent());
    EXPECT_FALSE (c.isSemiTransparent());
}

TEST (ColorTests, Uint32_Constructor)
{
    Color c (0x80ff00ff); // Semi-transparent magenta
    EXPECT_EQ (c.getARGB(), 0x80ff00ff);
    EXPECT_EQ (c.getAlpha(), 0x80);
    EXPECT_EQ (c.getRed(), 0xff);
    EXPECT_EQ (c.getGreen(), 0x00);
    EXPECT_EQ (c.getBlue(), 0xff);
    EXPECT_FALSE (c.isOpaque());
    EXPECT_FALSE (c.isTransparent());
    EXPECT_TRUE (c.isSemiTransparent());
}

TEST (ColorTests, RGB_Constructor)
{
    Color c (255, 128, 64);
    EXPECT_EQ (c.getAlpha(), 255);
    EXPECT_EQ (c.getRed(), 255);
    EXPECT_EQ (c.getGreen(), 128);
    EXPECT_EQ (c.getBlue(), 64);
    EXPECT_TRUE (c.isOpaque());
}

TEST (ColorTests, ARGB_Constructor)
{
    Color c (192, 255, 128, 64);
    EXPECT_EQ (c.getAlpha(), 192);
    EXPECT_EQ (c.getRed(), 255);
    EXPECT_EQ (c.getGreen(), 128);
    EXPECT_EQ (c.getBlue(), 64);
    EXPECT_TRUE (c.isSemiTransparent());
}

TEST (ColorTests, Copy_And_Move_Constructors)
{
    Color c1 (0xff123456);
    Color c2 (c1);
    Color c3 (std::move (c1));

    EXPECT_EQ (c2.getARGB(), 0xff123456);
    EXPECT_EQ (c3.getARGB(), 0xff123456);

    Color c4;
    c4 = c2;
    EXPECT_EQ (c4.getARGB(), 0xff123456);
}

TEST (ColorTests, Implicit_Conversion_To_Uint32)
{
    Color c (0xff123456);
    uint32 value = c;
    EXPECT_EQ (value, 0xff123456);
}

TEST (ColorTests, Transparency_Checks)
{
    Color opaque (0xffffffff);
    EXPECT_FALSE (opaque.isTransparent());
    EXPECT_FALSE (opaque.isSemiTransparent());
    EXPECT_TRUE (opaque.isOpaque());

    Color semiTransparent (0x80ffffff);
    EXPECT_FALSE (semiTransparent.isTransparent());
    EXPECT_TRUE (semiTransparent.isSemiTransparent());
    EXPECT_FALSE (semiTransparent.isOpaque());

    Color transparent (0x00ffffff);
    EXPECT_TRUE (transparent.isTransparent());
    EXPECT_TRUE (transparent.isSemiTransparent());
    EXPECT_FALSE (transparent.isOpaque());
}

TEST (ColorTests, Alpha_Operations)
{
    Color c (0xff123456);

    // Test getAlpha and getAlphaFloat
    EXPECT_EQ (c.getAlpha(), 255);
    EXPECT_NEAR (c.getAlphaFloat(), 1.0f, tol);

    // Test setAlpha
    c.setAlpha (uint8_t (128));
    EXPECT_EQ (c.getAlpha(), 128);
    EXPECT_NEAR (c.getAlphaFloat(), 128.0f / 255.0f, tol);

    // Test setAlpha with float
    c.setAlpha (0.5f);
    EXPECT_EQ (c.getAlpha(), 128); // 0.5 * 255 = 127.5, should round to 128

    // Test withAlpha
    Color c2 = c.withAlpha (uint8_t (64));
    EXPECT_EQ (c2.getAlpha(), 64);
    EXPECT_EQ (c.getAlpha(), 128); // Original unchanged

    // Test withAlpha with float
    Color c3 = c.withAlpha (0.25f);
    EXPECT_EQ (c3.getAlpha(), 64); // 0.25 * 255 = 63.75, should round to 64

    // Test withMultipliedAlpha
    Color c4 (0xff123456);
    Color c5 = c4.withMultipliedAlpha (uint8_t (128));
    EXPECT_EQ (c5.getAlpha(), 128); // 255 * (128/255) = 128

    Color c6 = c4.withMultipliedAlpha (0.5f);
    EXPECT_EQ (c6.getAlpha(), 128); // 255 * 0.5 = 127.5, should round to 128
}

TEST (ColorTests, Red_Operations)
{
    Color c (0xff123456);

    // Test getRed and getRedFloat
    EXPECT_EQ (c.getRed(), 0x12);
    EXPECT_NEAR (c.getRedFloat(), 0x12 / 255.0f, tol);

    // Test setRed
    c.setRed (uint8_t (200));
    EXPECT_EQ (c.getRed(), 200);

    // Test setRed with float
    c.setRed (0.5f);
    EXPECT_EQ (c.getRed(), 128); // 0.5 * 255 = 127.5, should round to 128

    // Test withRed
    Color c2 = c.withRed (uint8_t (100));
    EXPECT_EQ (c2.getRed(), 100);
    EXPECT_EQ (c.getRed(), 128); // Original unchanged

    // Test withRed with float
    Color c3 = c.withRed (0.8f);
    EXPECT_EQ (c3.getRed(), 204); // 0.8 * 255 rounded
}

TEST (ColorTests, Green_Operations)
{
    Color c (0xff123456);

    // Test getGreen and getGreenFloat
    EXPECT_EQ (c.getGreen(), 0x34);
    EXPECT_NEAR (c.getGreenFloat(), 0x34 / 255.0f, tol);

    // Test setGreen
    c.setGreen (uint8_t (150));
    EXPECT_EQ (c.getGreen(), 150);

    // Test setGreen with float
    c.setGreen (0.3f);
    EXPECT_EQ (c.getGreen(), 76); // 0.3 * 255 = 76.5, should round to 76

    // Test withGreen
    Color c2 = c.withGreen (uint8_t (75));
    EXPECT_EQ (c2.getGreen(), 75);
    EXPECT_EQ (c.getGreen(), 76); // Original unchanged

    // Test withGreen with float
    Color c3 = c.withGreen (0.9f);
    EXPECT_EQ (c3.getGreen(), 230); // 0.9 * 255 rounded
}

TEST (ColorTests, Blue_Operations)
{
    Color c (0xff123456);

    // Test getBlue and getBlueFloat
    EXPECT_EQ (c.getBlue(), 0x56);
    EXPECT_NEAR (c.getBlueFloat(), 0x56 / 255.0f, tol);

    // Test setBlue
    c.setBlue (uint8_t (200));
    EXPECT_EQ (c.getBlue(), 200);

    // Test setBlue with float
    c.setBlue (0.4f);
    EXPECT_EQ (c.getBlue(), 102); // 0.4 * 255 = 102.0, exact

    // Test withBlue
    Color c2 = c.withBlue (uint8_t (50));
    EXPECT_EQ (c2.getBlue(), 50);
    EXPECT_EQ (c.getBlue(), 102); // Original unchanged

    // Test withBlue with float
    Color c3 = c.withBlue (0.7f);
    EXPECT_EQ (c3.getBlue(), 178); // 0.7 * 255 rounded
}

TEST (ColorTests, HSL_Operations)
{
    // Test red color
    Color red (0xffff0000);
    EXPECT_NEAR (red.getHue(), 0.0f, tol);
    EXPECT_NEAR (red.getSaturation(), 1.0f, tol);
    EXPECT_NEAR (red.getLuminance(), 0.5f, tol);

    // Test green color
    Color green (0xff00ff00);
    EXPECT_NEAR (green.getHue(), 1.0f / 3.0f, tol);
    EXPECT_NEAR (green.getSaturation(), 1.0f, tol);
    EXPECT_NEAR (green.getLuminance(), 0.5f, tol);

    // Test blue color
    Color blue (0xff0000ff);
    EXPECT_NEAR (blue.getHue(), 2.0f / 3.0f, tol);
    EXPECT_NEAR (blue.getSaturation(), 1.0f, tol);
    EXPECT_NEAR (blue.getLuminance(), 0.5f, tol);

    // Test gray color
    Color gray (0xff808080);
    EXPECT_NEAR (gray.getHue(), 0.0f, tol);
    EXPECT_NEAR (gray.getSaturation(), 0.0f, tol);
    EXPECT_NEAR (gray.getLuminance(), 128.0f / 255.0f, tol); // 128/255 = 0.50196...

    // Test toHSL
    auto [h, s, l] = red.toHSL();
    EXPECT_NEAR (h, 0.0f, tol);
    EXPECT_NEAR (s, 1.0f, tol);
    EXPECT_NEAR (l, 0.5f, tol);

    // Test fromHSL
    Color fromHSL = Color::fromHSL (0.0f, 1.0f, 0.5f);
    EXPECT_EQ (fromHSL.getRed(), 255);
    EXPECT_EQ (fromHSL.getGreen(), 0);
    EXPECT_EQ (fromHSL.getBlue(), 0);

    // Test fromHSL with alpha
    Color fromHSLAlpha = Color::fromHSL (0.0f, 1.0f, 0.5f, 0.5f);
    EXPECT_EQ (fromHSLAlpha.getAlpha(), 127); // Implementation returns 127
}

TEST (ColorTests, HSV_Operations)
{
    // Test red color
    Color red (0xffff0000);
    auto [h, s, v] = red.toHSV();
    EXPECT_NEAR (h, 0.0f, tol);
    EXPECT_NEAR (s, 1.0f, tol);
    EXPECT_NEAR (v, 1.0f, tol);

    // Test green color
    Color green (0xff00ff00);
    auto [h2, s2, v2] = green.toHSV();
    EXPECT_NEAR (h2, 1.0f / 3.0f, tol);
    EXPECT_NEAR (s2, 1.0f, tol);
    EXPECT_NEAR (v2, 1.0f, tol);

    // Test fromHSV
    Color fromHSV = Color::fromHSV (0.0f, 1.0f, 1.0f);
    EXPECT_EQ (fromHSV.getRed(), 255);
    EXPECT_EQ (fromHSV.getGreen(), 0);
    EXPECT_EQ (fromHSV.getBlue(), 0);

    // Test fromHSV with alpha
    Color fromHSVAlpha = Color::fromHSV (0.0f, 1.0f, 1.0f, 0.5f);
    EXPECT_EQ (fromHSVAlpha.getAlpha(), 127); // Implementation returns 127
}

TEST (ColorTests, Brightness_Operations)
{
    Color c (0xff808080); // Gray

    // Test brighter
    Color brighter = c.brighter (0.2f);
    EXPECT_GT (brighter.getRed(), c.getRed());
    EXPECT_GT (brighter.getGreen(), c.getGreen());
    EXPECT_GT (brighter.getBlue(), c.getBlue());

    // Test darker
    Color darker = c.darker (0.2f);
    EXPECT_LT (darker.getRed(), c.getRed());
    EXPECT_LT (darker.getGreen(), c.getGreen());
    EXPECT_LT (darker.getBlue(), c.getBlue());

    // Test that brighter and darker are inverses
    Color roundTrip = c.brighter (0.1f).darker (0.1f);
    EXPECT_EQ (roundTrip.getRed(), c.getRed());
    EXPECT_EQ (roundTrip.getGreen(), c.getGreen());
    EXPECT_EQ (roundTrip.getBlue(), c.getBlue());
}

TEST (ColorTests, Contrasting_Operations)
{
    Color c (0xff8f808f);

    // Test contrasting
    Color contrasting = c.contrasting();
    EXPECT_NE (contrasting.getARGB(), c.getARGB());

    // Test contrasting with amount
    Color contrasting2 = c.contrasting (0.3f);
    EXPECT_NE (contrasting2.getARGB(), c.getARGB());
    EXPECT_NE (contrasting2.getARGB(), contrasting.getARGB());
}

TEST (ColorTests, Inversion_Operations)
{
    Color c (0xff123456);
    Color original = c;

    // Test invert
    c.invert();
    EXPECT_EQ (c.getRed(), 255 - original.getRed());
    EXPECT_EQ (c.getGreen(), 255 - original.getGreen());
    EXPECT_EQ (c.getBlue(), 255 - original.getBlue());
    EXPECT_EQ (c.getAlpha(), original.getAlpha()); // Alpha unchanged

    // Test inverted
    Color c2 (0xff123456);
    Color inverted = c2.inverted();
    EXPECT_EQ (inverted.getRed(), 255 - c2.getRed());
    EXPECT_EQ (inverted.getGreen(), 255 - c2.getGreen());
    EXPECT_EQ (inverted.getBlue(), 255 - c2.getBlue());
    EXPECT_EQ (inverted.getAlpha(), c2.getAlpha());
    EXPECT_EQ (c2.getARGB(), 0xff123456); // Original unchanged
}

TEST (ColorTests, Alpha_Inversion_Operations)
{
    Color c (0x80123456);
    Color original = c;

    // Test invertAlpha
    c.invertAlpha();
    EXPECT_EQ (c.getAlpha(), 255 - original.getAlpha());
    EXPECT_EQ (c.getRed(), original.getRed()); // RGB unchanged
    EXPECT_EQ (c.getGreen(), original.getGreen());
    EXPECT_EQ (c.getBlue(), original.getBlue());

    // Test invertedAlpha
    Color c2 (0x80123456);
    Color invertedAlpha = c2.invertedAlpha();
    EXPECT_EQ (invertedAlpha.getAlpha(), 255 - c2.getAlpha());
    EXPECT_EQ (invertedAlpha.getRed(), c2.getRed());
    EXPECT_EQ (invertedAlpha.getGreen(), c2.getGreen());
    EXPECT_EQ (invertedAlpha.getBlue(), c2.getBlue());
    EXPECT_EQ (c2.getARGB(), 0x80123456); // Original unchanged
}

TEST (ColorTests, Static_Factory_Methods)
{
    // Test fromRGB
    Color fromRGB = Color::fromRGB (255, 128, 64);
    EXPECT_EQ (fromRGB.getAlpha(), 255);
    EXPECT_EQ (fromRGB.getRed(), 255);
    EXPECT_EQ (fromRGB.getGreen(), 128);
    EXPECT_EQ (fromRGB.getBlue(), 64);

    // Test fromRGBA
    Color fromRGBA = Color::fromRGBA (255, 128, 64, 192);
    EXPECT_EQ (fromRGBA.getAlpha(), 192);
    EXPECT_EQ (fromRGBA.getRed(), 255);
    EXPECT_EQ (fromRGBA.getGreen(), 128);
    EXPECT_EQ (fromRGBA.getBlue(), 64);

    // Test fromARGB
    Color fromARGB = Color::fromARGB (192, 255, 128, 64);
    EXPECT_EQ (fromARGB.getAlpha(), 192);
    EXPECT_EQ (fromARGB.getRed(), 255);
    EXPECT_EQ (fromARGB.getGreen(), 128);
    EXPECT_EQ (fromARGB.getBlue(), 64);

    // Test fromBGRA
    Color fromBGRA = Color::fromBGRA (64, 128, 255, 192);
    EXPECT_EQ (fromBGRA.getAlpha(), 192);
    EXPECT_EQ (fromBGRA.getRed(), 255);
    EXPECT_EQ (fromBGRA.getGreen(), 128);
    EXPECT_EQ (fromBGRA.getBlue(), 64);
}

TEST (ColorTests, String_Operations)
{
    Color c (0xff123456);

    // Test toString
    String hexString = c.toString();
    EXPECT_TRUE (hexString.startsWith ("#"));
    EXPECT_EQ (hexString.length(), 9); // #RRGGBBAA

    // Test toStringRGB
    String rgbString = c.toStringRGB (false);
    EXPECT_TRUE (rgbString.startsWith ("rgb("));
    EXPECT_TRUE (rgbString.endsWith (")"));

    String rgbaString = c.toStringRGB (true);
    EXPECT_TRUE (rgbaString.startsWith ("rgb("));
    EXPECT_TRUE (rgbaString.endsWith (")"));

    // Test fromString with hex
    Color fromHex = Color::fromString ("#ff0000");
    EXPECT_EQ (fromHex.getRed(), 255);
    EXPECT_EQ (fromHex.getGreen(), 0);
    EXPECT_EQ (fromHex.getBlue(), 0);

    // Test fromString with short hex
    Color fromShortHex = Color::fromString ("#f00");
    EXPECT_EQ (fromShortHex.getRed(), 255);
    EXPECT_EQ (fromShortHex.getGreen(), 0);
    EXPECT_EQ (fromShortHex.getBlue(), 0);

    // Test fromString with RGB
    Color fromRGB = Color::fromString ("rgb(255, 128, 64)");
    EXPECT_EQ (fromRGB.getRed(), 255);
    EXPECT_EQ (fromRGB.getGreen(), 128);
    EXPECT_EQ (fromRGB.getBlue(), 64);

    // Test fromString with RGBA
    Color fromRGBA = Color::fromString ("rgba(255, 128, 64, 192)");
    EXPECT_EQ (fromRGBA.getRed(), 255);
    EXPECT_EQ (fromRGBA.getGreen(), 128);
    EXPECT_EQ (fromRGBA.getBlue(), 64);
    EXPECT_EQ (fromRGBA.getAlpha(), 192);

    // Test fromString with named color
    Color fromNamed = Color::fromString ("red");
    EXPECT_EQ (fromNamed.getRed(), 255);
    EXPECT_EQ (fromNamed.getGreen(), 0);
    EXPECT_EQ (fromNamed.getBlue(), 0);

    // Test fromString with invalid string
    Color fromInvalid = Color::fromString ("invalid");
    EXPECT_EQ (fromInvalid.getARGB(), 0); // Should return default/empty color
}

TEST (ColorTests, Random_Color)
{
    // Test opaqueRandom
    Color random1 = Color::opaqueRandom();
    Color random2 = Color::opaqueRandom();

    EXPECT_EQ (random1.getAlpha(), 255);
    EXPECT_EQ (random2.getAlpha(), 255);

    // Colors should be different (with very high probability)
    EXPECT_NE (random1.getARGB(), random2.getARGB());
}

TEST (ColorTests, Edge_Cases)
{
    // Test clamping in setters
    Color c;
    c.setAlpha (2.0f); // Should clamp to 1.0
    EXPECT_EQ (c.getAlpha(), 255);

    c.setAlpha (-1.0f); // Should clamp to 0.0
    EXPECT_EQ (c.getAlpha(), 0);

    c.setRed (2.0f);
    EXPECT_EQ (c.getRed(), 255);

    c.setGreen (-1.0f);
    EXPECT_EQ (c.getGreen(), 0);

    c.setBlue (2.0f);
    EXPECT_EQ (c.getBlue(), 255);

    // Test extreme HSL values
    Color fromHSL = Color::fromHSL (2.0f, 1.0f, 0.5f); // Hue > 1.0
    EXPECT_NO_THROW (fromHSL.getRed());                // Should not crash

    // Test extreme HSV values
    Color fromHSV = Color::fromHSV (2.0f, 1.0f, 1.0f); // Hue > 1.0
    EXPECT_NO_THROW (fromHSV.getRed());                // Should not crash
}

TEST (ColorTests, Boundary_Values)
{
    // Test minimum values
    Color minColor (0x00000000);
    EXPECT_EQ (minColor.getAlpha(), 0);
    EXPECT_EQ (minColor.getRed(), 0);
    EXPECT_EQ (minColor.getGreen(), 0);
    EXPECT_EQ (minColor.getBlue(), 0);
    EXPECT_TRUE (minColor.isTransparent());

    // Test maximum values
    Color maxColor (0xffffffff);
    EXPECT_EQ (maxColor.getAlpha(), 255);
    EXPECT_EQ (maxColor.getRed(), 255);
    EXPECT_EQ (maxColor.getGreen(), 255);
    EXPECT_EQ (maxColor.getBlue(), 255);
    EXPECT_TRUE (maxColor.isOpaque());

    // Test fromString edge cases
    Color emptyString = Color::fromString ("");
    EXPECT_EQ (emptyString.getARGB(), Colors::transparentBlack);

    Color invalidHex = Color::fromString ("#gggggg");
    EXPECT_EQ (invalidHex.getARGB(), Color (0xffefefef));

    Color invalidRGB = Color::fromString ("rgb(999, 999, 999)");
    EXPECT_EQ (invalidRGB.getRed(), 231); // 999 % 256 = 231
}

TEST (ColorTests, HSL_Round_Trip)
{
    Color original (0xff8040c0);
    auto [h, s, l] = original.toHSL();
    Color roundTrip = Color::fromHSL (h, s, l);

    // Allow some tolerance for floating point precision
    EXPECT_NEAR (original.getRed(), roundTrip.getRed(), 2);
    EXPECT_NEAR (original.getGreen(), roundTrip.getGreen(), 2);
    EXPECT_NEAR (original.getBlue(), roundTrip.getBlue(), 2);
}

TEST (ColorTests, HSV_Round_Trip)
{
    Color original (0xff8040c0);
    auto [h, s, v] = original.toHSV();
    Color roundTrip = Color::fromHSV (h, s, v);

    // Allow some tolerance for floating point precision
    EXPECT_NEAR (original.getRed(), roundTrip.getRed(), 2);
    EXPECT_NEAR (original.getGreen(), roundTrip.getGreen(), 2);
    EXPECT_NEAR (original.getBlue(), roundTrip.getBlue(), 2);
}

TEST (ColorTests, Chaining_Operations)
{
    Color c (0xff123456);

    // Test method chaining
    Color result = c.setRed (uint8_t (255)).setGreen (uint8_t (128)).setBlue (uint8_t (64)).setAlpha (uint8_t (192));
    EXPECT_EQ (result.getAlpha(), 192);
    EXPECT_EQ (result.getRed(), 255);
    EXPECT_EQ (result.getGreen(), 128);
    EXPECT_EQ (result.getBlue(), 64);

    // Test with* methods don't modify original
    Color original (0xff123456);
    Color modified = original.withRed (uint8_t (255)).withGreen (uint8_t (128)).withBlue (uint8_t (64)).withAlpha (uint8_t (192));
    EXPECT_EQ (original.getARGB(), 0xff123456);
    EXPECT_EQ (modified.getAlpha(), 192);
    EXPECT_EQ (modified.getRed(), 255);
    EXPECT_EQ (modified.getGreen(), 128);
    EXPECT_EQ (modified.getBlue(), 64);
}
