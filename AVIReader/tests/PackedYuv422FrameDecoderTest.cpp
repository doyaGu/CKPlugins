// ===========================================================================
// PackedYuv422FrameDecoderTest.cpp -- unit tests for PackedYuv422FrameDecoder.
// Tests YUY2 and UYVY macropixel decoding.
// ===========================================================================

#include <gtest/gtest.h>

#include "FrameDecoder.h"
#include "TestUtils.h"

#include <vector>
#include <cstdint>
#include <cmath>

using namespace testutil;

namespace {

// Reference YUV -> RGB conversion matching the decoder's formula.
void RefYuvToRgb(uint8_t y, uint8_t u, uint8_t v,
                 uint8_t &outR, uint8_t &outG, uint8_t &outB)
{
    int c = static_cast<int>(y) - 16;
    int d = static_cast<int>(u) - 128;
    int e = static_cast<int>(v) - 128;

    int r = (298 * c + 409 * e + 128) >> 8;
    int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int b = (298 * c + 516 * d + 128) >> 8;

    outR = static_cast<uint8_t>(std::max(0, std::min(255, r)));
    outG = static_cast<uint8_t>(std::max(0, std::min(255, g)));
    outB = static_cast<uint8_t>(std::max(0, std::min(255, b)));
}

} // namespace

// ===========================================================================
// YUY2 layout
// ===========================================================================

TEST(PackedYuv422Decoder, Yuy2_BasicMacropixel)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_YUY2, 2, 1, 16);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // YUY2 macropixel: [Y0, U, Y1, V]
    const uint8_t Y0 = 180, U = 100, Y1 = 200, V = 150;
    std::vector<uint8_t> src = {Y0, U, Y1, V};

    int dstStride = 2 * 4;
    std::vector<uint8_t> dst(dstStride, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 2, 1, dst.data(), dstStride));

    // Compute expected RGB
    uint8_t r0, g0, b0, r1, g1, b1;
    RefYuvToRgb(Y0, U, V, r0, g0, b0);
    RefYuvToRgb(Y1, U, V, r1, g1, b1);

    // Output is BGRA
    EXPECT_EQ(dst[0], b0);
    EXPECT_EQ(dst[1], g0);
    EXPECT_EQ(dst[2], r0);
    EXPECT_EQ(dst[3], 0xFF);

    EXPECT_EQ(dst[4], b1);
    EXPECT_EQ(dst[5], g1);
    EXPECT_EQ(dst[6], r1);
    EXPECT_EQ(dst[7], 0xFF);
}

TEST(PackedYuv422Decoder, Yuy2_MultiRow)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_YUY2, 2, 2, 16);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // 2 rows, each with one macropixel (2 pixels)
    std::vector<uint8_t> src = {
        128, 128, 128, 128, // row 0: Y=128, U=128, V=128 (should be ~grey)
        235, 128, 235, 128  // row 1: Y=235, U=128, V=128 (should be ~white)
    };

    int dstStride = 2 * 4;
    std::vector<uint8_t> dst(dstStride * 2, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 2, 2, dst.data(), dstStride));
    // Just verify it doesn't crash and writes something
    // Alpha bytes should be 0xFF
    EXPECT_EQ(dst[3], 0xFF);
    EXPECT_EQ(dst[dstStride + 3], 0xFF);
}

// ===========================================================================
// UYVY layout
// ===========================================================================

TEST(PackedYuv422Decoder, Uyvy_BasicMacropixel)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_UYVY, 2, 1, 16);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // UYVY: [U, Y0, V, Y1]
    const uint8_t Y0 = 180, U = 100, Y1 = 200, V = 150;
    std::vector<uint8_t> src = {U, Y0, V, Y1};

    int dstStride = 2 * 4;
    std::vector<uint8_t> dst(dstStride, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 2, 1, dst.data(), dstStride));

    uint8_t r0, g0, b0, r1, g1, b1;
    RefYuvToRgb(Y0, U, V, r0, g0, b0);
    RefYuvToRgb(Y1, U, V, r1, g1, b1);

    // Pixel 0
    EXPECT_EQ(dst[0], b0);
    EXPECT_EQ(dst[1], g0);
    EXPECT_EQ(dst[2], r0);
    EXPECT_EQ(dst[3], 0xFF);

    // Pixel 1
    EXPECT_EQ(dst[4], b1);
    EXPECT_EQ(dst[5], g1);
    EXPECT_EQ(dst[6], r1);
    EXPECT_EQ(dst[7], 0xFF);
}

// ===========================================================================
// Neutral YUV values
// ===========================================================================

TEST(PackedYuv422Decoder, NeutralGrey)
{
    // Y=128, U=128, V=128 -> should produce a grey-ish color
    auto info = MakeVideoStreamInfo(avi::kCodec_YUY2, 2, 1, 16);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> src = {128, 128, 128, 128};

    int dstStride = 8;
    std::vector<uint8_t> dst(dstStride, 0);

    EXPECT_TRUE(dec->Decode(src.data(), src.size(), 2, 1, dst.data(), dstStride));

    // R, G, B should all be close to the same value
    int r = dst[2], g = dst[1], b = dst[0];
    EXPECT_NEAR(r, g, 5);
    EXPECT_NEAR(g, b, 5);
}

// ===========================================================================
// Error cases
// ===========================================================================

TEST(PackedYuv422Decoder, OddWidthReturnsFalse)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_YUY2, 3, 1, 16);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> src(3 * 2, 0);
    std::vector<uint8_t> dst(3 * 4, 0);

    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 3, 1, dst.data(), 12));
}

TEST(PackedYuv422Decoder, NullInputReturnsFalse)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_YUY2, 2, 1, 16);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> dst(8, 0);
    EXPECT_FALSE(dec->Decode(nullptr, 4, 2, 1, dst.data(), 8));
}

TEST(PackedYuv422Decoder, NullOutputReturnsFalse)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_YUY2, 2, 1, 16);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> src(4, 0);
    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 2, 1, nullptr, 8));
}

TEST(PackedYuv422Decoder, InsufficientDataReturnsFalse)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_YUY2, 4, 4, 16);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> src(8, 0); // need 4*2*4=32 bytes
    std::vector<uint8_t> dst(4 * 4 * 4, 0);

    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 4, 4, dst.data(), 16));
}

TEST(PackedYuv422Decoder, InsufficientOutputStrideReturnsFalse)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_YUY2, 2, 1, 16);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> src = {128, 128, 128, 128};
    std::vector<uint8_t> dst(7, 0); // width * 4 would be 8
    EXPECT_FALSE(dec->Decode(src.data(), src.size(), 2, 1, dst.data(), 7));
}

// ===========================================================================
// NeedsSequentialFrames
// ===========================================================================

TEST(PackedYuv422Decoder, DoesNotNeedSequentialFrames)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_YUY2, 2, 1, 16);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);
    EXPECT_FALSE(dec->NeedsSequentialFrames());
}
