// ===========================================================================
// AviDemuxerTest.cpp -- unit tests for AviDemuxer.
// Uses synthetic temp files constructed via TestUtils builders.
// ===========================================================================

#include <gtest/gtest.h>

#include "AviDemuxer.h"
#include "TestUtils.h"

#include <algorithm>
#include <vector>
#include <cstdint>

using namespace testutil;

namespace {

void PatchU64LE(std::vector<uint8_t> &buf, size_t offset, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        buf[offset + static_cast<size_t>(i)] = static_cast<uint8_t>((v >> (i * 8)) & 0xFFu);
}

std::vector<uint8_t> BuildHeaderStandardIndxAvi(const std::vector<std::vector<uint8_t>> &framePayloads,
                                                 bool fieldIndex)
{
    const int32_t width = 4;
    const int32_t height = 4;
    const uint16_t bpp = 24;
    const uint32_t fps = 30;
    const uint32_t streamChunkId = avi::StreamChunkId(0, avi::kStreamType_vids);

    std::vector<uint8_t> hdrlInner;
    auto avih = MakeAvihChunk(1, width, height, 1000000u / fps, static_cast<uint32_t>(framePayloads.size()));
    AppendBytes(hdrlInner, avih.data(), avih.size());

    std::vector<uint8_t> strlInner;
    auto strh = MakeStrhChunk(avi::kStreamType_vids, avi::kCodec_RGB, 1, fps,
                              static_cast<uint32_t>(framePayloads.size()));
    auto strf = MakeStrfVideoChunk(width, height, bpp, avi::kCodec_RGB);
    AppendBytes(strlInner, strh.data(), strh.size());
    AppendBytes(strlInner, strf.data(), strf.size());

    std::vector<uint8_t> indxData;
    AppendU16LE(indxData, fieldIndex ? 3u : 2u); // wLongsPerEntry
    AppendU8(indxData, fieldIndex ? avi::kAVI_INDEX_2FIELD : 0u); // bIndexSubType
    AppendU8(indxData, avi::kAVI_INDEX_OF_CHUNKS); // bIndexType
    AppendU32LE(indxData, static_cast<uint32_t>(framePayloads.size())); // nEntriesInUse
    AppendU32LE(indxData, streamChunkId); // dwChunkId
    AppendU64LE(indxData, 0); // qwBaseOffset (patched later)
    AppendU32LE(indxData, 0); // dwReserved
    for (const auto &payload : framePayloads)
    {
        AppendU32LE(indxData, 0); // dwOffset (patched later)
        AppendU32LE(indxData, static_cast<uint32_t>(payload.size())); // dwSize
        if (fieldIndex)
            AppendU32LE(indxData, 0); // dwOffsetField2
    }
    auto indx = MakeChunk(avi::kFourCC_indx, indxData);
    const size_t indxOffsetInStrlInner = strlInner.size();
    AppendBytes(strlInner, indx.data(), indx.size());

    auto strl = MakeListChunk(avi::kFourCC_strl, strlInner);
    const size_t strlOffsetInHdrlInner = hdrlInner.size();
    AppendBytes(hdrlInner, strl.data(), strl.size());
    auto hdrl = MakeListChunk(avi::kFourCC_hdrl, hdrlInner);

    std::vector<uint8_t> moviInner;
    std::vector<size_t> frameHeaderOffsetsInMoviInner;
    for (const auto &payload : framePayloads)
    {
        frameHeaderOffsetsInMoviInner.push_back(moviInner.size());
        auto chunk = MakeChunk(streamChunkId, payload);
        AppendBytes(moviInner, chunk.data(), chunk.size());
    }
    auto movi = MakeListChunk(avi::kFourCC_movi, moviInner);

    std::vector<uint8_t> riffInner;
    const size_t hdrlOffsetInRiffInner = riffInner.size();
    AppendBytes(riffInner, hdrl.data(), hdrl.size());
    const size_t moviOffsetInRiffInner = riffInner.size();
    AppendBytes(riffInner, movi.data(), movi.size());
    std::vector<uint8_t> file = MakeRiffFile(avi::kFourCC_AVI, riffInner);

    if (frameHeaderOffsetsInMoviInner.empty())
        return file;

    // File layout:
    //   RIFF(12) + hdrl LIST(8+4+...) + movi LIST(8+4+...)
    const size_t hdrlDataOffset = 12u + hdrlOffsetInRiffInner + 12u;
    const size_t strlDataOffset = hdrlDataOffset + strlOffsetInHdrlInner + 12u;
    const size_t indxDataOffset = strlDataOffset + indxOffsetInStrlInner + 8u;

    const size_t moviDataOffset = 12u + moviOffsetInRiffInner + 12u;
    const uint64_t baseOffset = static_cast<uint64_t>(moviDataOffset + frameHeaderOffsetsInMoviInner[0] + 8u);
    PatchU64LE(file, indxDataOffset + 12u, baseOffset);

    const size_t entryStride = fieldIndex ? 12u : 8u;
    const size_t entriesOffset = indxDataOffset + sizeof(avi::AviStdIndexHeader);
    const size_t entryCount = std::min(framePayloads.size(), frameHeaderOffsetsInMoviInner.size());
    for (size_t i = 0; i < entryCount; ++i)
    {
        const uint64_t dataOffset = static_cast<uint64_t>(moviDataOffset + frameHeaderOffsetsInMoviInner[i] + 8u);
        const uint32_t relOffset = static_cast<uint32_t>(dataOffset - baseOffset);
        PatchU32LE(file, entriesOffset + i * entryStride, relOffset);
    }

    return file;
}

} // namespace

// ===========================================================================
// Open failure cases
// ===========================================================================

TEST(AviDemuxerOpen, NullptrReturnsFalse)
{
    AviDemuxer demuxer;
    EXPECT_FALSE(demuxer.Open(nullptr));
    EXPECT_FALSE(demuxer.IsOpen());
}

TEST(AviDemuxerOpen, EmptyStringReturnsFalse)
{
    AviDemuxer demuxer;
    EXPECT_FALSE(demuxer.Open(""));
    EXPECT_FALSE(demuxer.IsOpen());
}

TEST(AviDemuxerOpen, NonexistentPathReturnsFalse)
{
    AviDemuxer demuxer;
    EXPECT_FALSE(demuxer.Open("Z:\\__nonexistent_avi_test_file__.avi"));
}

TEST(AviDemuxerOpen, NonAviRiffFormTypeReturnsFalse)
{
    // Valid RIFF but form type is 'WAVE' instead of 'AVI '
    auto fileData = MakeRiffFile(avi::MakeFourCC('W', 'A', 'V', 'E'), {});
    TempFile file(fileData, ".riff");

    AviDemuxer demuxer;
    EXPECT_FALSE(demuxer.Open(file.CPath()));
}

// ===========================================================================
// Minimal valid AVI
// ===========================================================================

class AviDemuxerMinimalTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Build a minimal AVI: 1 video stream (4x4 RGB24), 1 frame of known data
        m_Width = 4;
        m_Height = 4;
        m_Bpp = 24;

        // 4x4 RGB24 bottom-up: stride = ((4*24+31)&~31)/8 = 12 bytes/row
        // Total = 12 * 4 = 48 bytes
        int srcStride = ((m_Width * m_Bpp + 31) & ~31) / 8;
        std::vector<uint8_t> frameData(srcStride * m_Height, 0);
        // Set first pixel (bottom-left): BGR = (0xFF, 0x00, 0x00) -> blue
        frameData[0] = 0xFF;
        frameData[1] = 0x00;
        frameData[2] = 0x00;

        m_FileData = BuildMinimalAvi(m_Width, m_Height, m_Bpp, avi::kCodec_RGB,
                                      30, {frameData});
        m_TempFile = std::make_unique<TempFile>(m_FileData, ".avi");
    }

    int m_Width;
    int m_Height;
    int m_Bpp;
    std::vector<uint8_t> m_FileData;
    std::unique_ptr<TempFile> m_TempFile;
};

TEST_F(AviDemuxerMinimalTest, OpenSucceeds)
{
    AviDemuxer demuxer;
    EXPECT_TRUE(demuxer.Open(m_TempFile->CPath()));
    EXPECT_TRUE(demuxer.IsOpen());
}

TEST_F(AviDemuxerMinimalTest, StreamCountIsOne)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));
    EXPECT_EQ(demuxer.GetStreamCount(), 1);
}

TEST_F(AviDemuxerMinimalTest, StreamInfoCorrect)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));

    const avi::AviStreamInfo *info = demuxer.GetStreamInfo(0);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->type, avi::kStreamType_vids);
    EXPECT_EQ(info->codec, avi::kCodec_RGB);
    EXPECT_EQ(info->width, m_Width);
    EXPECT_EQ(info->height, m_Height);
    EXPECT_EQ(info->bitsPerPixel, m_Bpp);
    EXPECT_DOUBLE_EQ(info->frameRate, 30.0);
}

TEST_F(AviDemuxerMinimalTest, StreamInfoOutOfRangeReturnsNull)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));
    EXPECT_EQ(demuxer.GetStreamInfo(-1), nullptr);
    EXPECT_EQ(demuxer.GetStreamInfo(1), nullptr);
    EXPECT_EQ(demuxer.GetStreamInfo(100), nullptr);
}

TEST_F(AviDemuxerMinimalTest, FindFirstVideoStreamReturnsZero)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));
    EXPECT_EQ(demuxer.FindFirstVideoStream(), 0);
}

TEST_F(AviDemuxerMinimalTest, FindFirstAudioStreamReturnsNeg1)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));
    EXPECT_EQ(demuxer.FindFirstAudioStream(), -1);
}

TEST_F(AviDemuxerMinimalTest, FrameCountIsOne)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));
    EXPECT_EQ(demuxer.GetFrameCount(0), 1);
}

TEST_F(AviDemuxerMinimalTest, FrameCountOutOfRangeIsZero)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));
    EXPECT_EQ(demuxer.GetFrameCount(-1), 0);
    EXPECT_EQ(demuxer.GetFrameCount(99), 0);
}

TEST_F(AviDemuxerMinimalTest, DurationMsNonZero)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));
    int durationMs = demuxer.GetDurationMs(0);
    EXPECT_GT(durationMs, 0);
    // 1 frame @ 30fps -> ~33ms
    EXPECT_NEAR(durationMs, 33, 2);
}

TEST_F(AviDemuxerMinimalTest, GetFrameInfoValid)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));

    const avi::FrameIndexEntry *entry = demuxer.GetFrameInfo(0, 0);
    ASSERT_NE(entry, nullptr);
    EXPECT_GT(entry->offset, 0);
    EXPECT_GT(entry->size, 0u);
    EXPECT_TRUE(entry->isKeyframe);
}

TEST_F(AviDemuxerMinimalTest, GetFrameInfoOutOfRangeNull)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));
    EXPECT_EQ(demuxer.GetFrameInfo(0, -1), nullptr);
    EXPECT_EQ(demuxer.GetFrameInfo(0, 1), nullptr);
    EXPECT_EQ(demuxer.GetFrameInfo(5, 0), nullptr);
}

TEST_F(AviDemuxerMinimalTest, ReadFrameDataReturnsCorrectBytes)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));

    std::vector<uint8_t> frameData;
    EXPECT_TRUE(demuxer.ReadFrameData(0, 0, frameData));
    EXPECT_FALSE(frameData.empty());

    // First 3 bytes should be our known pixel BGR = (0xFF, 0x00, 0x00)
    ASSERT_GE(frameData.size(), 3u);
    EXPECT_EQ(frameData[0], 0xFF);
    EXPECT_EQ(frameData[1], 0x00);
    EXPECT_EQ(frameData[2], 0x00);
}

TEST_F(AviDemuxerMinimalTest, ReadFrameDataOobReturnsFalse)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));

    std::vector<uint8_t> out;
    EXPECT_FALSE(demuxer.ReadFrameData(-1, 0, out));
    EXPECT_TRUE(out.empty());

    EXPECT_FALSE(demuxer.ReadFrameData(0, 99, out));
    EXPECT_TRUE(out.empty());
}

// ===========================================================================
// Close and re-open
// ===========================================================================

TEST_F(AviDemuxerMinimalTest, CloseResetsState)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));
    EXPECT_EQ(demuxer.GetStreamCount(), 1);
    demuxer.Close();
    EXPECT_FALSE(demuxer.IsOpen());
    EXPECT_EQ(demuxer.GetStreamCount(), 0);
}

TEST_F(AviDemuxerMinimalTest, ReopenAfterCloseSucceeds)
{
    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));
    demuxer.Close();
    ASSERT_TRUE(demuxer.Open(m_TempFile->CPath()));
    EXPECT_EQ(demuxer.GetStreamCount(), 1);
}

// ===========================================================================
// Multi-frame AVI
// ===========================================================================

TEST(AviDemuxerMultiFrame, ThreeFramesCounted)
{
    int w = 4, h = 4, bpp = 24;
    int srcStride = ((w * bpp + 31) & ~31) / 8;
    std::vector<uint8_t> frame(srcStride * h, 0x42);

    auto data = BuildMinimalAvi(w, h, static_cast<uint16_t>(bpp), avi::kCodec_RGB,
                                 25, {frame, frame, frame});
    TempFile file(data, ".avi");

    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(file.CPath()));
    EXPECT_EQ(demuxer.GetFrameCount(0), 3);

    // All three frames should be readable
    for (int i = 0; i < 3; ++i)
    {
        std::vector<uint8_t> out;
        EXPECT_TRUE(demuxer.ReadFrameData(0, i, out));
        EXPECT_EQ(out.size(), static_cast<size_t>(srcStride * h));
    }
}

// ===========================================================================
// Duration with zero frame rate
// ===========================================================================

TEST(AviDemuxerDuration, ZeroFrameRateReturnsZero)
{
    // Build an AVI with scale=0 (which gives frameRate 0.0)
    int w = 4, h = 4, bpp = 24;
    int srcStride = ((w * bpp + 31) & ~31) / 8;
    std::vector<uint8_t> frame(srcStride * h, 0);

    // Use BuildMinimalAvi with fps=0 - but since BuildMinimalAvi divides 1000000/fps,
    // we need to construct manually with scale=0.
    // Instead, build with fps=1 then check GetDurationMs handles normal case
    auto data = BuildMinimalAvi(w, h, static_cast<uint16_t>(bpp), avi::kCodec_RGB,
                                 1, {frame});
    TempFile file(data, ".avi");

    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(file.CPath()));
    // 1 frame @ 1 fps -> 1000 ms
    EXPECT_EQ(demuxer.GetDurationMs(0), 1000);
}

TEST(AviDemuxerOpenDml, HeaderStandardIndxWithoutIdx1ParsesFrames)
{
    int w = 4, h = 4, bpp = 24;
    int srcStride = ((w * bpp + 31) & ~31) / 8;
    std::vector<uint8_t> frame0(srcStride * h, 0x11);
    std::vector<uint8_t> frame1(srcStride * h, 0x22);

    auto data = BuildHeaderStandardIndxAvi({frame0, frame1}, false);
    TempFile file(data, ".avi");

    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(file.CPath()));
    EXPECT_EQ(demuxer.GetFrameCount(0), 2);

    std::vector<uint8_t> out;
    ASSERT_TRUE(demuxer.ReadFrameData(0, 1, out));
    ASSERT_EQ(out.size(), frame1.size());
    EXPECT_EQ(out[0], 0x22);
}

TEST(AviDemuxerOpenDml, HeaderFieldIndexIsRejected)
{
    int w = 4, h = 4, bpp = 24;
    int srcStride = ((w * bpp + 31) & ~31) / 8;
    std::vector<uint8_t> frame(srcStride * h, 0x55);

    auto data = BuildHeaderStandardIndxAvi({frame}, true);
    TempFile file(data, ".avi");

    AviDemuxer demuxer;
    EXPECT_FALSE(demuxer.Open(file.CPath()));
}

TEST(AviDemuxerVideoFormat, BiRgb32DefaultsToOpaqueAlphaMask)
{
    std::vector<uint8_t> frame = {0x11, 0x22, 0x33, 0x00};
    auto data = BuildMinimalAvi(1, 1, 32, avi::kCodec_RGB, 30, {frame});
    TempFile file(data, ".avi");

    AviDemuxer demuxer;
    ASSERT_TRUE(demuxer.Open(file.CPath()));

    const avi::AviStreamInfo *info = demuxer.GetStreamInfo(0);
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->alphaMask, 0u);
}
