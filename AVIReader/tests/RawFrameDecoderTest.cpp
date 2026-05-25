// ===========================================================================
// RawFrameDecoderTest.cpp -- unit tests for RawFrameDecoder.
// Tests 8/16/24/32 bpp decode, top-down/bottom-up, error cases.
// ===========================================================================

#include <gtest/gtest.h>

#include "FrameDecoder.h"
#include "TestUtils.h"

#include <vector>
#include <cstdint>
#include <cstring>

using namespace testutil;

namespace {

// Helper: aligned stride matching the source code
int AlignedStride(int width, int bpp)
{
    return ((width * bpp + 31) & ~31) / 8;
}

} // namespace

// ===========================================================================
// 24bpp decode
// ===========================================================================

TEST(RawFrameDecoder, Decode24bpp_1x1_Pixel)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 1, 1, 24);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Source: BGR layout. B=0xFF, G=0x00, R=0x80
    int srcStride = AlignedStride(1, 24);
    std::vector<uint8_t> src(srcStride, 0);
    src[0] = 0xFF; // B
    src[1] = 0x00; // G
    src[2] = 0x80; // R

    int dstStride = 4; // 1 pixel * 4 bytes
    std::vector<uint8_t> dst(dstStride, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 1, 1, dst.data(), dstStride));

    // Output is BGRA
    EXPECT_EQ(dst[0], 0xFF); // B
    EXPECT_EQ(dst[1], 0x00); // G
    EXPECT_EQ(dst[2], 0x80); // R
    EXPECT_EQ(dst[3], 0xFF); // A (always 0xFF for 24bpp)
}

TEST(RawFrameDecoder, Decode24bpp_BottomUpLayout)
{
    // 2x2 image: different colors in each quadrant
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 2, 2, 24);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    int srcStride = AlignedStride(2, 24);
    std::vector<uint8_t> src(srcStride * 2, 0);

    // Row 0 (bottom row in image): pixel(0,0)=red, pixel(1,0)=green
    src[0] = 0x00; src[1] = 0x00; src[2] = 0xFF; // red BGR
    src[3] = 0x00; src[4] = 0xFF; src[5] = 0x00; // green BGR
    // Row 1 (top row in image): pixel(0,1)=blue, pixel(1,1)=white
    src[srcStride + 0] = 0xFF; src[srcStride + 1] = 0x00; src[srcStride + 2] = 0x00; // blue
    src[srcStride + 3] = 0xFF; src[srcStride + 4] = 0xFF; src[srcStride + 5] = 0xFF; // white

    int dstStride = 8; // 2 pixels * 4 bytes
    std::vector<uint8_t> dst(dstStride * 2, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 2, 2, dst.data(), dstStride));

    // Output row 0 should be the bottom source row (red, green)
    EXPECT_EQ(dst[0], 0x00); // B of pixel (0,0)
    EXPECT_EQ(dst[2], 0xFF); // R of pixel (0,0)
}

// ===========================================================================
// 32bpp decode
// ===========================================================================

TEST(RawFrameDecoder, Decode32bpp_1x1_Pixel)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 1, 1, 32,
                                     false, 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Source pixel: BGRA = (0x11, 0x22, 0x33, 0x44)
    std::vector<uint8_t> src = {0x11, 0x22, 0x33, 0x44};
    int dstStride = 4;
    std::vector<uint8_t> dst(4, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 1, 1, dst.data(), dstStride));

    // The decoder extracts channels via masks:
    // Pixel word = 0x44332211
    // Blue = mask 0x000000FF -> 0x11
    // Green = mask 0x0000FF00 -> 0x22
    // Red = mask 0x00FF0000 -> 0x33
    // Alpha = mask 0xFF000000 -> 0x44
    EXPECT_EQ(dst[0], 0x11); // B
    EXPECT_EQ(dst[1], 0x22); // G
    EXPECT_EQ(dst[2], 0x33); // R
    EXPECT_EQ(dst[3], 0x44); // A
}

TEST(RawFrameDecoder, Decode32bpp_ZeroAlphaMaskForcesOpaque)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 1, 1, 32,
                                     false, 0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Source alpha byte is zero, but BI_RGB 32bpp should decode as opaque unless an alpha mask is present.
    std::vector<uint8_t> src = {0x11, 0x22, 0x33, 0x00};
    std::vector<uint8_t> dst(4, 0);
    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 1, 1, dst.data(), 4));
    EXPECT_EQ(dst[3], 0xFF);
}

// ===========================================================================
// 16bpp default 555 masks
// ===========================================================================

TEST(RawFrameDecoder, Decode16bpp_Default555)
{
    // No explicit masks -> default 5-5-5
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 1, 1, 16);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Encode max red (bits 10-14 all set): 0x7C00
    int srcStride = AlignedStride(1, 16);
    std::vector<uint8_t> src(srcStride, 0);
    src[0] = 0x00;
    src[1] = 0x7C; // 0x7C00 LE

    int dstStride = 4;
    std::vector<uint8_t> dst(4, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 1, 1, dst.data(), dstStride));

    // Red channel should be fully expanded to 0xFF (5-bit max -> 8-bit max)
    EXPECT_EQ(dst[2], 0xFF); // R
    EXPECT_EQ(dst[0], 0x00); // B
    EXPECT_EQ(dst[1], 0x00); // G
    EXPECT_EQ(dst[3], 0xFF); // A
}

// ===========================================================================
// 16bpp explicit BITFIELDS (565)
// ===========================================================================

TEST(RawFrameDecoder, Decode16bpp_Bitfields565)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_BITFIELDS, 1, 1, 16,
                                     false, 0xF800u, 0x07E0u, 0x001Fu);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Encode max green (bits 5-10): 0x07E0
    int srcStride = AlignedStride(1, 16);
    std::vector<uint8_t> src(srcStride, 0);
    src[0] = 0xE0;
    src[1] = 0x07; // 0x07E0 LE

    int dstStride = 4;
    std::vector<uint8_t> dst(4, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 1, 1, dst.data(), dstStride));

    // Green should be ~0xFF (6-bit max -> 8-bit)
    EXPECT_GE(dst[1], 0xFCu); // G (252-255 due to bit expansion)
    EXPECT_EQ(dst[0], 0x00);  // B
    EXPECT_EQ(dst[2], 0x00);  // R
}

// ===========================================================================
// 8bpp with palette
// ===========================================================================

TEST(RawFrameDecoder, Decode8bpp_PaletteIndex)
{
    // Palette: index 0 = 0xFF0000FF (ARGB for blue), index 1 = 0xFF00FF00 (green)
    std::vector<uint32_t> palette(256, 0xFF000000u);
    palette[0] = 0xFF0000FFu; // ARGB blue
    palette[1] = 0xFF00FF00u; // ARGB green

    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 2, 1, 8,
                                     false, 0, 0, 0, 0, palette);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    int srcStride = AlignedStride(2, 8);
    std::vector<uint8_t> src(srcStride, 0);
    src[0] = 0; // palette index 0
    src[1] = 1; // palette index 1

    int dstStride = 8;
    std::vector<uint8_t> dst(dstStride, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 2, 1, dst.data(), dstStride));

    // Pixel 0: palette[0] = 0xFF0000FF -> BGRA bytes: B=0xFF, G=0x00, R=0x00, A=0xFF
    EXPECT_EQ(dst[0], 0xFF); // B
    EXPECT_EQ(dst[1], 0x00); // G
    EXPECT_EQ(dst[2], 0x00); // R
    EXPECT_EQ(dst[3], 0xFF); // A

    // Pixel 1: palette[1] = 0xFF00FF00 -> BGRA: B=0x00, G=0xFF, R=0x00, A=0xFF
    EXPECT_EQ(dst[4], 0x00); // B
    EXPECT_EQ(dst[5], 0xFF); // G
    EXPECT_EQ(dst[6], 0x00); // R
    EXPECT_EQ(dst[7], 0xFF); // A
}

TEST(RawFrameDecoder, Decode4bpp_PaletteNibbles)
{
    std::vector<uint32_t> palette(16, 0xFF000000u);
    palette[1] = 0xFF0000FFu; // blue
    palette[2] = 0xFF00FF00u; // green
    palette[3] = 0xFFFF0000u; // red
    palette[4] = 0xFFFFFFFFu; // white

    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 4, 1, 4,
                                     false, 0, 0, 0, 0, palette);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    int srcStride = AlignedStride(4, 4);
    std::vector<uint8_t> src(srcStride, 0);
    src[0] = 0x12;
    src[1] = 0x34;

    int dstStride = 16;
    std::vector<uint8_t> dst(dstStride, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 4, 1, dst.data(), dstStride));

    EXPECT_EQ(dst[0], 0xFF);  // index 1 blue
    EXPECT_EQ(dst[5], 0xFF);  // index 2 green
    EXPECT_EQ(dst[10], 0xFF); // index 3 red
    EXPECT_EQ(dst[12], 0xFF); // index 4 white B
    EXPECT_EQ(dst[13], 0xFF); // index 4 white G
    EXPECT_EQ(dst[14], 0xFF); // index 4 white R
}

TEST(RawFrameDecoder, Decode1bpp_PaletteBits)
{
    std::vector<uint32_t> palette(2, 0xFF000000u);
    palette[0] = 0xFF000000u; // black
    palette[1] = 0xFFFFFFFFu; // white

    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 8, 1, 1,
                                     false, 0, 0, 0, 0, palette);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    int srcStride = AlignedStride(8, 1);
    std::vector<uint8_t> src(srcStride, 0);
    src[0] = 0xA5; // 10100101, high bit first

    int dstStride = 32;
    std::vector<uint8_t> dst(dstStride, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 8, 1, dst.data(), dstStride));

    const uint8_t expected[] = {0xFF, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0x00, 0xFF};
    for (int x = 0; x < 8; ++x)
    {
        EXPECT_EQ(dst[x * 4 + 0], expected[x]) << x;
        EXPECT_EQ(dst[x * 4 + 1], expected[x]) << x;
        EXPECT_EQ(dst[x * 4 + 2], expected[x]) << x;
        EXPECT_EQ(dst[x * 4 + 3], 0xFF) << x;
    }
}

// ===========================================================================
// Top-down source (negative biHeight)
// ===========================================================================

TEST(RawFrameDecoder, Decode24bpp_TopDown)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 1, 2, 24, true);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    int srcStride = AlignedStride(1, 24);
    std::vector<uint8_t> src(srcStride * 2, 0);

    // Row 0 in source (top of image): red pixel
    src[0] = 0x00; src[1] = 0x00; src[2] = 0xFF;
    // Row 1 in source (bottom of image): blue pixel
    src[srcStride + 0] = 0xFF; src[srcStride + 1] = 0x00; src[srcStride + 2] = 0x00;

    int dstStride = 4;
    std::vector<uint8_t> dst(dstStride * 2, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 1, 2, dst.data(), dstStride));

    // Output is bottom-up. Row 0 = bottom = blue pixel from source row 1.
    EXPECT_EQ(dst[0], 0xFF); // B (was source row 1)
    EXPECT_EQ(dst[2], 0x00); // R

    // Row 1 in output = top = red pixel from source row 0.
    EXPECT_EQ(dst[dstStride + 0], 0x00); // B
    EXPECT_EQ(dst[dstStride + 2], 0xFF); // R (was source row 0)
}

// ===========================================================================
// Error cases
// ===========================================================================

TEST(RawFrameDecoder, NullDataReturnsFalse)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 1, 1, 24);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> dst(4, 0);
    EXPECT_FALSE(dec->Decode(nullptr, 100, 1, 1, dst.data(), 4));
}

TEST(RawFrameDecoder, NullOutputReturnsFalse)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 1, 1, 24);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> src(4, 0);
    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 1, 1, nullptr, 4));
}

TEST(RawFrameDecoder, ZeroWidthReturnsFalse)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 1, 1, 24);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> src(4, 0);
    std::vector<uint8_t> dst(4, 0);
    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 0, 1, dst.data(), 4));
}

TEST(RawFrameDecoder, ZeroHeightReturnsFalse)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 1, 1, 24);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> src(4, 0);
    std::vector<uint8_t> dst(4, 0);
    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 1, 0, dst.data(), 4));
}

TEST(RawFrameDecoder, InsufficientDataReturnsFalse)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 4, 4, 24);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Need 48 bytes but only provide 10
    std::vector<uint8_t> src(10, 0);
    std::vector<uint8_t> dst(4 * 4 * 4, 0);
    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 4, 4, dst.data(), 16));
}

TEST(RawFrameDecoder, InsufficientOutputStrideReturnsFalse)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 2, 1, 24);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    int srcStride = AlignedStride(2, 24);
    std::vector<uint8_t> src(srcStride, 0);
    std::vector<uint8_t> dst(7, 0); // width * 4 would be 8
    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 2, 1, dst.data(), 7));
}
