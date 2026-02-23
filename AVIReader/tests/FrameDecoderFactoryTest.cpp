// ===========================================================================
// FrameDecoderFactoryTest.cpp -- tests for CreateFrameDecoder() factory.
// ===========================================================================

#include <gtest/gtest.h>

#include "FrameDecoder.h"
#include "TestUtils.h"

using namespace testutil;

// ===========================================================================
// Supported codecs return non-null
// ===========================================================================

TEST(FrameDecoderFactory, Rgb24ReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 4, 4, 24);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

TEST(FrameDecoderFactory, Rgb32ReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 4, 4, 32);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

TEST(FrameDecoderFactory, Rgb16ReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 4, 4, 16);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

TEST(FrameDecoderFactory, Rgb8ReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 4, 4, 8);
    info.palette.resize(256, 0xFF000000u);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

TEST(FrameDecoderFactory, BitfieldsReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_BITFIELDS, 4, 4, 16,
                                     false, 0xF800u, 0x07E0u, 0x001Fu);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

TEST(FrameDecoderFactory, Rle8ReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RLE8, 4, 4, 8);
    info.palette.resize(256, 0xFF000000u);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

TEST(FrameDecoderFactory, MjpgUppercaseReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_MJPG, 4, 4, 24);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

TEST(FrameDecoderFactory, MjpgLowercaseReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_mjpg, 4, 4, 24);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

TEST(FrameDecoderFactory, CramReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_CRAM, 4, 4, 16);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

TEST(FrameDecoderFactory, MsvcReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_MSVC, 4, 4, 16);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

TEST(FrameDecoderFactory, WhamReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_WHAM, 4, 4, 16);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

TEST(FrameDecoderFactory, Yuy2ReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_YUY2, 4, 4, 16);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

TEST(FrameDecoderFactory, UyvyReturnsDecoder)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_UYVY, 4, 4, 16);
    auto dec = CreateFrameDecoder(info);
    EXPECT_NE(dec, nullptr);
}

// ===========================================================================
// Unsupported codecs return nullptr
// ===========================================================================

TEST(FrameDecoderFactory, UnsupportedCodecReturnsNull)
{
    auto info = MakeVideoStreamInfo(avi::MakeFourCC('H', '2', '6', '4'), 4, 4, 24);
    auto dec = CreateFrameDecoder(info);
    EXPECT_EQ(dec, nullptr);
}

TEST(FrameDecoderFactory, Rle4ReturnsNull)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RLE4, 4, 4, 4);
    auto dec = CreateFrameDecoder(info);
    EXPECT_EQ(dec, nullptr);
}

TEST(FrameDecoderFactory, Rle8WrongBppReturnsNull)
{
    auto info = MakeVideoStreamInfo(avi::kCodec_RLE8, 4, 4, 16);
    auto dec = CreateFrameDecoder(info);
    EXPECT_EQ(dec, nullptr);
}

TEST(FrameDecoderFactory, CramWrongBppReturnsNull)
{
    // CRAM decoder requires 16bpp
    auto info = MakeVideoStreamInfo(avi::kCodec_CRAM, 4, 4, 8);
    auto dec = CreateFrameDecoder(info);
    EXPECT_EQ(dec, nullptr);
}

TEST(FrameDecoderFactory, RgbUnsupportedBppReturnsNull)
{
    // 4bpp RGB is not supported
    auto info = MakeVideoStreamInfo(avi::kCodec_RGB, 4, 4, 4);
    auto dec = CreateFrameDecoder(info);
    EXPECT_EQ(dec, nullptr);
}
