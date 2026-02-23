// ===========================================================================
// Msvideo1FrameDecoderTest.cpp -- unit tests for Msvideo1FrameDecoder
// (Microsoft Video 1 / CRAM, 16bpp).
// ===========================================================================

#include <gtest/gtest.h>

#include "FrameDecoder.h"
#include "TestUtils.h"

#include <vector>
#include <cstdint>
#include <cstring>

using namespace testutil;

namespace {

avi::AviStreamInfo MakeCramInfo(int w, int h)
{
    return MakeVideoStreamInfo(avi::kCodec_CRAM, w, h, 16);
}

// Pack a 15-bit RGB555 colour: R(10-14), G(5-9), B(0-4).
uint16_t PackRgb555(uint8_t r5, uint8_t g5, uint8_t b5)
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(r5 & 0x1F) << 10) |
        (static_cast<uint16_t>(g5 & 0x1F) << 5) |
        (b5 & 0x1F));
}

// Expand 5-bit to 8-bit (matching decoder's Expand5To8).
uint8_t Exp5to8(uint8_t v)
{
    return static_cast<uint8_t>((v << 3) | (v >> 2));
}

// Encode a 16LE value to bytes.
void PushU16LE(std::vector<uint8_t> &buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

} // namespace

// ===========================================================================
// Dimension checks
// ===========================================================================

TEST(Msvideo1Decoder, WidthNotMultipleOf4ReturnsFalse)
{
    auto info = MakeCramInfo(5, 4);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> src(64, 0);
    int stride = 5 * 4;
    std::vector<uint8_t> dst(stride * 4, 0);

    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 5, 4, dst.data(), stride));
}

TEST(Msvideo1Decoder, HeightNotMultipleOf4ReturnsFalse)
{
    auto info = MakeCramInfo(4, 5);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> src(64, 0);
    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride * 5, 0);

    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 4, 5, dst.data(), stride));
}

// ===========================================================================
// One-color block
// ===========================================================================

TEST(Msvideo1Decoder, OneColorBlock)
{
    // 4x4 image -> 1 block. One-color code: high bit set (byteB >= 0x80)
    // and it's NOT a skip code (0x84xx).
    auto info = MakeCramInfo(4, 4);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Pack a color: R=31, G=0, B=0 -> pure red in RGB555 = 0x7C00
    // With high bit set: 0x7C00 | 0x8000 = 0xFC00
    // But wait, for one-color mode byteB >= 0x80 and NOT 0x84xx skip code.
    // Actually: byteB >= 0x80 and the code path is the else branch.
    // The colour word is byteA | (byteB << 8), and then Rgb555ToArgb32 is called
    // after masking off bit 15 (c &= 0x7FFF).
    //
    // Let's use color green: R=0, G=31, B=0 -> RGB555 = 0x03E0
    // Set high bit: 0x03E0 | 0x8000 = 0x83E0
    // byteA = 0xE0, byteB = 0x83. byteB >= 0x80, and (0x83 & 0xFC) != 0x84.
    uint16_t colorWord = PackRgb555(0, 31, 0) | 0x8000u;

    std::vector<uint8_t> src;
    PushU16LE(src, colorWord);

    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride * 4, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 4, 4, dst.data(), stride));

    uint8_t expectedG = Exp5to8(31);

    // All 16 pixels should be green
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            uint8_t *p = &dst[y * stride + x * 4];
            EXPECT_EQ(p[0], 0x00) << "B at (" << x << "," << y << ")";
            EXPECT_EQ(p[1], expectedG) << "G at (" << x << "," << y << ")";
            EXPECT_EQ(p[2], 0x00) << "R at (" << x << "," << y << ")";
            EXPECT_EQ(p[3], 0xFF) << "A at (" << x << "," << y << ")";
        }
    }
}

// ===========================================================================
// Two-color block
// ===========================================================================

TEST(Msvideo1Decoder, TwoColorBlock)
{
    // 4x4 image, 1 block. Two-color mode:
    // - 2 bytes: flags word (byteB must be < 0x80 to select two-color path)
    // - 2 bytes: color0 (RGB555, bit 15 = 0)
    // - 2 bytes: color1 (RGB555, bit 15 = 0)
    // Decoder: bit=0 -> (0^1)=1 -> color1; bit=1 -> (1^1)=0 -> color0
    // NOTE: flags=0x0000 triggers end-marker when totalBlocks==0.
    auto info = MakeCramInfo(4, 4);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    uint16_t c0 = PackRgb555(31, 0, 0); // 0x7C00 red
    uint16_t c1 = PackRgb555(0, 0, 31); // 0x001F blue

    // flags = 0x00FF: bottom 8 bits=1 (color0=red), top 8 bits=0 (color1=blue)
    // byteA=0xFF (nonzero -> no end marker), byteB=0x00 < 0x80 -> two-color mode
    // Rows 0,1 -> red; rows 2,3 -> blue
    uint16_t flags = 0x00FF;

    std::vector<uint8_t> src;
    PushU16LE(src, flags);
    PushU16LE(src, c0);
    PushU16LE(src, c1);

    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride * 4, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 4, 4, dst.data(), stride));

    uint8_t expRedR = Exp5to8(31);
    uint8_t expBlueB = Exp5to8(31);

    // Rows 0 and 1: all pixels = color0 (red)
    for (int y = 0; y < 2; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            uint8_t *p = &dst[y * stride + x * 4];
            EXPECT_EQ(p[0], 0x00) << "B at (" << x << "," << y << ")";
            EXPECT_EQ(p[2], expRedR) << "R at (" << x << "," << y << ")";
            EXPECT_EQ(p[3], 0xFF) << "A at (" << x << "," << y << ")";
        }
    }

    // Rows 2 and 3: all pixels = color1 (blue)
    for (int y = 2; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            uint8_t *p = &dst[y * stride + x * 4];
            EXPECT_EQ(p[0], expBlueB) << "B at (" << x << "," << y << ")";
            EXPECT_EQ(p[2], 0x00) << "R at (" << x << "," << y << ")";
            EXPECT_EQ(p[3], 0xFF) << "A at (" << x << "," << y << ")";
        }
    }
}

TEST(Msvideo1Decoder, TwoColorBlockCheckerboard)
{
    auto info = MakeCramInfo(4, 4);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    uint16_t c0 = PackRgb555(31, 31, 31); // white
    uint16_t c1 = PackRgb555(0, 0, 0);     // black

    // Alternating bits: 0x5555 = 0101_0101_0101_0101 in binary
    // byteB = 0x55 < 0x80 -> two-color mode
    // bit=1 -> color0 (white), bit=0 -> color1 (black)
    uint16_t flags = 0x5555;

    std::vector<uint8_t> src;
    PushU16LE(src, flags);
    PushU16LE(src, c0);
    PushU16LE(src, c1);

    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride * 4, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 4, 4, dst.data(), stride));

    // Verify alternating pixels
    uint8_t whiteR = Exp5to8(31);
    bool foundWhite = false, foundBlack = false;
    for (int i = 0; i < 16; ++i)
    {
        uint8_t r = dst[i * 4 + 2];
        if (r == whiteR) foundWhite = true;
        if (r == 0) foundBlack = true;
    }
    EXPECT_TRUE(foundWhite);
    EXPECT_TRUE(foundBlack);
}

// ===========================================================================
// Skip code
// ===========================================================================

TEST(Msvideo1Decoder, SkipCodePreservesPreviousFrame)
{
    // 4x4 -> 1 block. First decode fills with green, second decode uses skip.
    auto info = MakeCramInfo(4, 4);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride * 4, 0);

    // Frame 1: one-color green
    uint16_t greenWord = PackRgb555(0, 31, 0) | 0x8000u;
    std::vector<uint8_t> frame1;
    PushU16LE(frame1, greenWord);

    ASSERT_TRUE(dec->Decode(frame1.data(), frame1.size(), 4, 4, dst.data(), stride));

    // Frame 2: skip code. Skip-code format: byteA=count_lo, byteB in [0x84..0x87].
    // count = ((byteB - 0x84) << 8) + byteA. Skip code skips `count` blocks.
    // For 1 block: count=1 -> byteA=1, byteB=0x84.
    std::vector<uint8_t> frame2 = {0x01, 0x84};

    ASSERT_TRUE(dec->Decode(frame2.data(), frame2.size(), 4, 4, dst.data(), stride));

    uint8_t expectedG = Exp5to8(31);

    // Pixels should still be green from frame 1 (skip preserved them)
    EXPECT_EQ(dst[1], expectedG); // G of pixel (0,0)
}

// ===========================================================================
// DeltaFrameDecoder properties
// ===========================================================================

TEST(Msvideo1Decoder, NeedsSequentialFrames)
{
    auto info = MakeCramInfo(4, 4);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);
    EXPECT_TRUE(dec->NeedsSequentialFrames());
}

TEST(Msvideo1Decoder, ResetClearsState)
{
    auto info = MakeCramInfo(4, 4);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride * 4, 0);

    // Frame 1: one-color red
    uint16_t redWord = PackRgb555(31, 0, 0) | 0x8000u;
    std::vector<uint8_t> frame1;
    PushU16LE(frame1, redWord);
    ASSERT_TRUE(dec->Decode(frame1.data(), frame1.size(), 4, 4, dst.data(), stride));

    dec->Reset();

    // After reset, skip code should give zeros (no previous frame)
    std::vector<uint8_t> skipFrame = {0x01, 0x84};
    std::fill(dst.begin(), dst.end(), 0xCC);
    ASSERT_TRUE(dec->Decode(skipFrame.data(), skipFrame.size(), 4, 4, dst.data(), stride));

    // After reset, previous frame is all zeros -> pixels should be zero
    EXPECT_EQ(dst[0], 0x00);
    EXPECT_EQ(dst[1], 0x00);
    EXPECT_EQ(dst[2], 0x00);
}

// ===========================================================================
// Error cases
// ===========================================================================

TEST(Msvideo1Decoder, NullDataReturnsFalse)
{
    auto info = MakeCramInfo(4, 4);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> dst(64, 0);
    EXPECT_FALSE(dec->Decode(nullptr, 100, 4, 4, dst.data(), 16));
}

TEST(Msvideo1Decoder, ZeroDimensionsReturnsFalse)
{
    auto info = MakeCramInfo(4, 4);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> src(64, 0);
    std::vector<uint8_t> dst(64, 0);
    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 0, 4, dst.data(), 16));
    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 4, 0, dst.data(), 16));
}

TEST(Msvideo1Decoder, TruncatedDataReturnsFalse)
{
    auto info = MakeCramInfo(4, 4);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Only 1 byte - can't read the 2-byte block header
    std::vector<uint8_t> src = {0x00};
    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride * 4, 0);

    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 4, 4, dst.data(), stride));
}
