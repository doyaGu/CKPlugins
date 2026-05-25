// ===========================================================================
// Rle4FrameDecoderTest.cpp -- unit tests for Rle4FrameDecoder (BI_RLE4).
// ===========================================================================

#include <gtest/gtest.h>

#include "FrameDecoder.h"
#include "TestUtils.h"

#include <vector>
#include <cstdint>

using namespace testutil;

namespace {

avi::AviStreamInfo MakeRle4Info(int w, int h, const std::vector<uint32_t> &palette = {})
{
    avi::AviStreamInfo info;
    info.type = avi::kStreamType_vids;
    info.codec = avi::kCodec_RLE4;
    info.width = w;
    info.height = h;
    info.bitsPerPixel = 4;
    info.videoTopDown = false;
    info.palette = palette;
    info.frameRate = 30.0;
    info.totalFrames = 1;
    return info;
}

std::vector<uint32_t> MakeGreyPalette()
{
    std::vector<uint32_t> pal(16);
    for (int i = 0; i < 16; ++i)
        pal[i] = 0xFF000000u |
                 (static_cast<uint32_t>(i) << 16) |
                 (static_cast<uint32_t>(i) << 8)  |
                 static_cast<uint32_t>(i);
    return pal;
}

} // namespace

TEST(Rle4FrameDecoder, RunEncodingAlternatesNibbles)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle4Info(4, 1, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> rle = {
        0x04, 0xAB,
        0x00, 0x01
    };

    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride, 0);

    EXPECT_TRUE(dec->Decode(rle.data(), rle.size(), 4, 1, dst.data(), stride));

    EXPECT_EQ(dst[0 * 4], 0x0A);
    EXPECT_EQ(dst[1 * 4], 0x0B);
    EXPECT_EQ(dst[2 * 4], 0x0A);
    EXPECT_EQ(dst[3 * 4], 0x0B);
}

TEST(Rle4FrameDecoder, AbsoluteRunUsesPackedNibblesAndWordPadding)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle4Info(5, 1, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> rle = {
        0x00, 0x05, 0x12, 0x34, 0x50, 0x00,
        0x00, 0x01
    };

    int stride = 5 * 4;
    std::vector<uint8_t> dst(stride, 0);

    EXPECT_TRUE(dec->Decode(rle.data(), rle.size(), 5, 1, dst.data(), stride));

    EXPECT_EQ(dst[0 * 4], 0x01);
    EXPECT_EQ(dst[1 * 4], 0x02);
    EXPECT_EQ(dst[2 * 4], 0x03);
    EXPECT_EQ(dst[3 * 4], 0x04);
    EXPECT_EQ(dst[4 * 4], 0x05);
}

TEST(Rle4FrameDecoder, DeltaEscapeMovesWriteCursor)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle4Info(4, 4, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> rle = {
        0x00, 0x02, 0x02, 0x01,
        0x01, 0x90,
        0x00, 0x01
    };

    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride * 4, 0);

    EXPECT_TRUE(dec->Decode(rle.data(), rle.size(), 4, 4, dst.data(), stride));

    uint8_t *pixel = &dst[1 * stride + 2 * 4];
    EXPECT_EQ(pixel[0], 0x09);
    EXPECT_EQ(pixel[1], 0x09);
    EXPECT_EQ(pixel[2], 0x09);
    EXPECT_EQ(pixel[3], 0xFF);
}

TEST(Rle4FrameDecoder, TruncatedAbsoluteRunReturnsFalse)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle4Info(5, 1, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> rle = {
        0x00, 0x05, 0x12, 0x34
    };

    int stride = 5 * 4;
    std::vector<uint8_t> dst(stride, 0);

    EXPECT_FALSE(dec->Decode(rle.data(), rle.size(), 5, 1, dst.data(), stride));
}
