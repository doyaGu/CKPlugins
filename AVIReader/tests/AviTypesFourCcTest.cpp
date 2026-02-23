// ===========================================================================
// AviTypesFourCcTest.cpp -- unit tests for AviTypes.h inline helpers.
// ===========================================================================

#include <gtest/gtest.h>

#include "AviTypes.h"

// ===========================================================================
// MakeFourCC
// ===========================================================================

TEST(MakeFourCC, KnownConstants)
{
    EXPECT_EQ(avi::MakeFourCC('R', 'I', 'F', 'F'), avi::kFourCC_RIFF);
    EXPECT_EQ(avi::MakeFourCC('L', 'I', 'S', 'T'), avi::kFourCC_LIST);
    EXPECT_EQ(avi::MakeFourCC('A', 'V', 'I', ' '), avi::kFourCC_AVI);
    EXPECT_EQ(avi::MakeFourCC('J', 'U', 'N', 'K'), avi::kFourCC_JUNK);
    EXPECT_EQ(avi::MakeFourCC('h', 'd', 'r', 'l'), avi::kFourCC_hdrl);
    EXPECT_EQ(avi::MakeFourCC('m', 'o', 'v', 'i'), avi::kFourCC_movi);
    EXPECT_EQ(avi::MakeFourCC('i', 'd', 'x', '1'), avi::kFourCC_idx1);
}

TEST(MakeFourCC, AllZeros)
{
    EXPECT_EQ(avi::MakeFourCC('\0', '\0', '\0', '\0'), 0u);
}

TEST(MakeFourCC, HighBits)
{
    // Chars with the high bit set should still be treated as unsigned bytes.
    uint32_t v = avi::MakeFourCC('\xFF', '\x00', '\x80', '\x01');
    EXPECT_EQ(v & 0xFF, 0xFFu);
    EXPECT_EQ((v >> 8) & 0xFF, 0x00u);
    EXPECT_EQ((v >> 16) & 0xFF, 0x80u);
    EXPECT_EQ((v >> 24) & 0xFF, 0x01u);
}

TEST(MakeFourCC, IsConstexpr)
{
    // Verify the result is usable at compile time.
    constexpr uint32_t cc = avi::MakeFourCC('T', 'E', 'S', 'T');
    static_assert(cc != 0, "MakeFourCC should be constexpr");
    EXPECT_EQ(cc, avi::MakeFourCC('T', 'E', 'S', 'T'));
}

// ===========================================================================
// ReadLe16 / ReadLe32 / ReadLe64
// ===========================================================================

TEST(ReadLe16, RoundTrip)
{
    const uint8_t data[] = {0x34, 0x12};
    EXPECT_EQ(avi::ReadLe16(data), 0x1234u);
}

TEST(ReadLe16, AllZeros)
{
    const uint8_t data[] = {0x00, 0x00};
    EXPECT_EQ(avi::ReadLe16(data), 0x0000u);
}

TEST(ReadLe16, AllOnes)
{
    const uint8_t data[] = {0xFF, 0xFF};
    EXPECT_EQ(avi::ReadLe16(data), 0xFFFFu);
}

TEST(ReadLe32, RoundTrip)
{
    const uint8_t data[] = {0x78, 0x56, 0x34, 0x12};
    EXPECT_EQ(avi::ReadLe32(data), 0x12345678u);
}

TEST(ReadLe32, AllZeros)
{
    const uint8_t data[] = {0, 0, 0, 0};
    EXPECT_EQ(avi::ReadLe32(data), 0u);
}

TEST(ReadLe32, AllOnes)
{
    const uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(avi::ReadLe32(data), 0xFFFFFFFFu);
}

TEST(ReadLe32, MixedBoundary)
{
    const uint8_t data[] = {0x00, 0xFF, 0x00, 0xFF};
    EXPECT_EQ(avi::ReadLe32(data), 0xFF00FF00u);
}

TEST(ReadLe64, RoundTrip)
{
    const uint8_t data[] = {0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12};
    EXPECT_EQ(avi::ReadLe64(data), 0x1234567890ABCDEFull);
}

TEST(ReadLe64, AllOnes)
{
    const uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    EXPECT_EQ(avi::ReadLe64(data), 0xFFFFFFFFFFFFFFFFull);
}

// ===========================================================================
// StreamChunkSuffix
// ===========================================================================

TEST(StreamChunkSuffix, VideoReturns_dc)
{
    EXPECT_STREQ(avi::StreamChunkSuffix(avi::kStreamType_vids), "dc");
}

TEST(StreamChunkSuffix, AudioReturns_wb)
{
    EXPECT_STREQ(avi::StreamChunkSuffix(avi::kStreamType_auds), "wb");
}

TEST(StreamChunkSuffix, UnknownReturns_QuestionMarks)
{
    EXPECT_STREQ(avi::StreamChunkSuffix(0xDEADBEEFu), "??");
}

// ===========================================================================
// StreamChunkId
// ===========================================================================

TEST(StreamChunkId, Stream0Video)
{
    uint32_t id = avi::StreamChunkId(0, avi::kStreamType_vids);
    EXPECT_EQ(id, avi::MakeFourCC('0', '0', 'd', 'c'));
}

TEST(StreamChunkId, Stream1Audio)
{
    uint32_t id = avi::StreamChunkId(1, avi::kStreamType_auds);
    EXPECT_EQ(id, avi::MakeFourCC('0', '1', 'w', 'b'));
}

TEST(StreamChunkId, Stream9Video)
{
    uint32_t id = avi::StreamChunkId(9, avi::kStreamType_vids);
    EXPECT_EQ(id, avi::MakeFourCC('0', '9', 'd', 'c'));
}

TEST(StreamChunkId, Stream10Audio)
{
    uint32_t id = avi::StreamChunkId(10, avi::kStreamType_auds);
    EXPECT_EQ(id, avi::MakeFourCC('1', '0', 'w', 'b'));
}

TEST(StreamChunkId, Stream99Video)
{
    uint32_t id = avi::StreamChunkId(99, avi::kStreamType_vids);
    EXPECT_EQ(id, avi::MakeFourCC('9', '9', 'd', 'c'));
}

// ===========================================================================
// StreamIndexFromChunkId
// ===========================================================================

TEST(StreamIndexFromChunkId, ValidStream0_dc)
{
    uint32_t id = avi::MakeFourCC('0', '0', 'd', 'c');
    EXPECT_EQ(avi::StreamIndexFromChunkId(id), 0);
}

TEST(StreamIndexFromChunkId, ValidStream1_wb)
{
    uint32_t id = avi::MakeFourCC('0', '1', 'w', 'b');
    EXPECT_EQ(avi::StreamIndexFromChunkId(id), 1);
}

TEST(StreamIndexFromChunkId, ValidStream10)
{
    uint32_t id = avi::MakeFourCC('1', '0', 'd', 'c');
    EXPECT_EQ(avi::StreamIndexFromChunkId(id), 10);
}

TEST(StreamIndexFromChunkId, InvalidNonDigitFirstChar)
{
    uint32_t id = avi::MakeFourCC('A', '0', 'd', 'c');
    EXPECT_EQ(avi::StreamIndexFromChunkId(id), -1);
}

TEST(StreamIndexFromChunkId, InvalidNonDigitSecondChar)
{
    uint32_t id = avi::MakeFourCC('0', 'X', 'd', 'c');
    EXPECT_EQ(avi::StreamIndexFromChunkId(id), -1);
}

TEST(StreamIndexFromChunkId, RoundTripWithStreamChunkId)
{
    for (int i = 0; i <= 99; ++i)
    {
        uint32_t id = avi::StreamChunkId(i, avi::kStreamType_vids);
        EXPECT_EQ(avi::StreamIndexFromChunkId(id), i);
    }
}
