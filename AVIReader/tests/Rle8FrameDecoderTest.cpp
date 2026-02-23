// ===========================================================================
// Rle8FrameDecoderTest.cpp -- unit tests for Rle8FrameDecoder (BI_RLE8).
// ===========================================================================

#include <gtest/gtest.h>

#include "FrameDecoder.h"
#include "TestUtils.h"

#include <vector>
#include <cstdint>

using namespace testutil;

namespace {

avi::AviStreamInfo MakeRle8Info(int w, int h, const std::vector<uint32_t> &palette = {})
{
    avi::AviStreamInfo info;
    info.type = avi::kStreamType_vids;
    info.codec = avi::kCodec_RLE8;
    info.width = w;
    info.height = h;
    info.bitsPerPixel = 8;
    info.videoTopDown = false;
    info.palette = palette;
    info.frameRate = 30.0;
    info.totalFrames = 1;
    return info;
}

// Build a simple palette: index i -> ARGB (0xFF, i, i, i)
std::vector<uint32_t> MakeGreyPalette()
{
    std::vector<uint32_t> pal(256);
    for (int i = 0; i < 256; ++i)
        pal[i] = 0xFF000000u |
                 (static_cast<uint32_t>(i) << 16) |
                 (static_cast<uint32_t>(i) << 8)  |
                 static_cast<uint32_t>(i);
    return pal;
}

} // namespace

// ===========================================================================
// End-of-bitmap only (empty image)
// ===========================================================================

TEST(Rle8FrameDecoder, EndOfBitmapOnly)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle8Info(4, 4, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // RLE data: just end-of-bitmap escape
    std::vector<uint8_t> rle = {0x00, 0x01}; // EOB

    int stride = 4 * 4; // 4 pixels * 4 bytes
    std::vector<uint8_t> dst(stride * 4, 0xCC); // fill with sentinel

    EXPECT_TRUE(dec->Decode(rle.data(), rle.size(), 4, 4, dst.data(), stride));
    // DeltaFrameDecoder initializes to zeros, so all pixels should be 0x00000000
    // (m_PreviousFrame starts zeroed)
    EXPECT_EQ(dst[0], 0x00);
}

// ===========================================================================
// Run-length encoding
// ===========================================================================

TEST(Rle8FrameDecoder, RunEncoding)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle8Info(4, 1, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // RLE: fill 4 pixels with palette index 42, then EOB
    std::vector<uint8_t> rle = {
        0x04, 0x2A, // run of 4, index 42
        0x00, 0x01  // EOB
    };

    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride, 0);

    EXPECT_TRUE(dec->Decode(rle.data(), rle.size(), 4, 1, dst.data(), stride));

    // Each pixel should be palette[42] = 0xFF2A2A2A in BGRA: (0x2A, 0x2A, 0x2A, 0xFF)
    for (int x = 0; x < 4; ++x)
    {
        EXPECT_EQ(dst[x * 4 + 0], 0x2A) << "pixel " << x << " B";
        EXPECT_EQ(dst[x * 4 + 1], 0x2A) << "pixel " << x << " G";
        EXPECT_EQ(dst[x * 4 + 2], 0x2A) << "pixel " << x << " R";
        EXPECT_EQ(dst[x * 4 + 3], 0xFF) << "pixel " << x << " A";
    }
}

// ===========================================================================
// End-of-line escape
// ===========================================================================

TEST(Rle8FrameDecoder, EndOfLineAdvancesRow)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle8Info(2, 2, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Fill row 0 with index 10, EOL, fill row 1 with index 20, EOB
    // Note: RLE8 bottom-up, so row 0 = bottom row in output
    std::vector<uint8_t> rle = {
        0x02, 0x0A,       // run of 2, index 10 (bottom row)
        0x00, 0x00,       // end-of-line
        0x02, 0x14,       // run of 2, index 20 (next row up)
        0x00, 0x01        // EOB
    };

    int stride = 2 * 4;
    std::vector<uint8_t> dst(stride * 2, 0);

    EXPECT_TRUE(dec->Decode(rle.data(), rle.size(), 2, 2, dst.data(), stride));

    // Row 0 (bottom): palette[10] = 0xFF0A0A0A
    EXPECT_EQ(dst[0], 0x0A);
    EXPECT_EQ(dst[3], 0xFF);

    // Row 1 (top): palette[20] = 0xFF141414
    EXPECT_EQ(dst[stride + 0], 0x14);
    EXPECT_EQ(dst[stride + 3], 0xFF);
}

// ===========================================================================
// Absolute run
// ===========================================================================

TEST(Rle8FrameDecoder, AbsoluteRun)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle8Info(4, 1, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Absolute run of 4 pixels: indices 5, 10, 15, 20
    // N=4 is even, so no pad byte needed
    std::vector<uint8_t> rle = {
        0x00, 0x04, 0x05, 0x0A, 0x0F, 0x14, // absolute run
        0x00, 0x01                             // EOB
    };

    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride, 0);

    EXPECT_TRUE(dec->Decode(rle.data(), rle.size(), 4, 1, dst.data(), stride));

    EXPECT_EQ(dst[0 * 4], 0x05); // palette[5] B component
    EXPECT_EQ(dst[1 * 4], 0x0A); // palette[10]
    EXPECT_EQ(dst[2 * 4], 0x0F); // palette[15]
    EXPECT_EQ(dst[3 * 4], 0x14); // palette[20]
}

TEST(Rle8FrameDecoder, AbsoluteRunOddPadByte)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle8Info(4, 1, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Absolute run of 3 pixels (odd -> 1 pad byte after)
    std::vector<uint8_t> rle = {
        0x00, 0x03, 0x05, 0x0A, 0x0F, 0x00, // absolute run (3 pixels + pad)
        0x01, 0x14,                            // run of 1, index 20 for 4th pixel
        0x00, 0x01                             // EOB
    };

    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride, 0);

    EXPECT_TRUE(dec->Decode(rle.data(), rle.size(), 4, 1, dst.data(), stride));

    EXPECT_EQ(dst[0 * 4], 0x05);
    EXPECT_EQ(dst[1 * 4], 0x0A);
    EXPECT_EQ(dst[2 * 4], 0x0F);
    EXPECT_EQ(dst[3 * 4], 0x14);
}

// ===========================================================================
// Delta escape
// ===========================================================================

TEST(Rle8FrameDecoder, DeltaEscape)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle8Info(4, 4, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Start at (0,0), delta to (2,1), then fill one pixel with index 99, then EOB
    std::vector<uint8_t> rle = {
        0x00, 0x02, 0x02, 0x01, // delta: dx=2, dy=1
        0x01, 0x63,             // run of 1, index 99
        0x00, 0x01              // EOB
    };

    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride * 4, 0);

    EXPECT_TRUE(dec->Decode(rle.data(), rle.size(), 4, 4, dst.data(), stride));

    // Pixel at (2, 1) should be palette[99] = 0xFF636363
    uint8_t *pixel = &dst[1 * stride + 2 * 4];
    EXPECT_EQ(pixel[0], 0x63); // B
    EXPECT_EQ(pixel[1], 0x63); // G
    EXPECT_EQ(pixel[2], 0x63); // R
    EXPECT_EQ(pixel[3], 0xFF); // A
}

// ===========================================================================
// DeltaFrameDecoder base class
// ===========================================================================

TEST(Rle8FrameDecoder, ResetClearsPrevious)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle8Info(4, 4, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Decode one frame
    std::vector<uint8_t> rle = {0x00, 0x01};
    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride * 4, 0);
    dec->Decode(rle.data(), rle.size(), 4, 4, dst.data(), stride);

    dec->Reset();
    EXPECT_TRUE(dec->NeedsSequentialFrames());

    // After reset, decoding again should start from all-zeros
    std::fill(dst.begin(), dst.end(), 0xCC);
    EXPECT_TRUE(dec->Decode(rle.data(), rle.size(), 4, 4, dst.data(), stride));
    EXPECT_EQ(dst[0], 0x00); // fresh zero-initialized previous frame
}

// ===========================================================================
// Error cases
// ===========================================================================

TEST(Rle8FrameDecoder, NullDataReturnsFalse)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle8Info(4, 4, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> dst(64, 0);
    EXPECT_FALSE(dec->Decode(nullptr, 100, 4, 4, dst.data(), 16));
}

TEST(Rle8FrameDecoder, TruncatedDataReturnsFalse)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle8Info(4, 4, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    // Only 1 byte - can't even read a 2-byte code
    std::vector<uint8_t> rle = {0x04};
    int stride = 4 * 4;
    std::vector<uint8_t> dst(stride * 4, 0);

    EXPECT_FALSE(dec->Decode(rle.data(), rle.size(), 4, 4, dst.data(), stride));
}

TEST(Rle8FrameDecoder, InsufficientOutputStrideReturnsFalse)
{
    auto pal = MakeGreyPalette();
    auto info = MakeRle8Info(2, 1, pal);
    auto dec = CreateFrameDecoder(info);
    ASSERT_NE(dec, nullptr);

    std::vector<uint8_t> rle = {0x02, 0x01, 0x00, 0x01};
    std::vector<uint8_t> dst(7, 0); // width * 4 would be 8
    EXPECT_FALSE(dec->Decode(rle.data(), rle.size(), 2, 1, dst.data(), 7));
}
