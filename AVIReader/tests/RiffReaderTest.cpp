// ===========================================================================
// RiffReaderTest.cpp -- unit tests for RiffReader.
// Uses synthetic temp files (no committed binary fixtures needed).
// ===========================================================================

#include <gtest/gtest.h>

#include "RiffReader.h"
#include "TestUtils.h"

#include <vector>
#include <cstdint>

using namespace testutil;

// ===========================================================================
// Open / Close
// ===========================================================================

TEST(RiffReaderOpen, NullptrReturnsFalse)
{
    RiffReader reader;
    EXPECT_FALSE(reader.Open(nullptr));
    EXPECT_FALSE(reader.IsOpen());
}

TEST(RiffReaderOpen, EmptyStringReturnsFalse)
{
    RiffReader reader;
    EXPECT_FALSE(reader.Open(""));
    EXPECT_FALSE(reader.IsOpen());
}

TEST(RiffReaderOpen, NonexistentPathReturnsFalse)
{
    RiffReader reader;
    EXPECT_FALSE(reader.Open("Z:\\__nonexistent_avireader_test_file__.bin"));
    EXPECT_FALSE(reader.IsOpen());
}

TEST(RiffReaderOpen, FileSmallerThan12BytesReturnsFalse)
{
    // 8 bytes: RIFF + size but no form type
    std::vector<uint8_t> data;
    AppendU32LE(data, avi::kFourCC_RIFF);
    AppendU32LE(data, 0);
    TempFile file(data);

    RiffReader reader;
    EXPECT_FALSE(reader.Open(file.CPath()));
}

TEST(RiffReaderOpen, ValidMinimalRiffSucceeds)
{
    // 12 bytes: RIFF header with form type JUNK
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, {});
    TempFile file(fileData);

    RiffReader reader;
    EXPECT_TRUE(reader.Open(file.CPath()));
    EXPECT_TRUE(reader.IsOpen());
    EXPECT_EQ(reader.FileSize(), static_cast<int64_t>(fileData.size()));
}

TEST(RiffReaderOpen, NonRiffMagicFailsOnChunkRead)
{
    // Valid size but first 4 bytes are not RIFF/LIST.
    std::vector<uint8_t> data;
    AppendU32LE(data, avi::MakeFourCC('F', 'A', 'K', 'E'));
    AppendU32LE(data, 4);
    AppendU32LE(data, avi::kFourCC_JUNK);
    TempFile file(data);

    RiffReader reader;
    EXPECT_TRUE(reader.Open(file.CPath())); // Open succeeds (file >= 12 bytes)
    RiffReader::Chunk chunk;
    // ReadChunkHeader succeeds but the id won't be RIFF
    EXPECT_TRUE(reader.ReadChunkHeader(chunk));
    EXPECT_NE(chunk.id, avi::kFourCC_RIFF);
}

TEST(RiffReaderClose, DoubleCloseSafe)
{
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, {});
    TempFile file(fileData);

    RiffReader reader;
    EXPECT_TRUE(reader.Open(file.CPath()));
    reader.Close();
    EXPECT_FALSE(reader.IsOpen());
    reader.Close(); // Should not crash
    EXPECT_FALSE(reader.IsOpen());
}

TEST(RiffReaderClose, ResetsState)
{
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, {});
    TempFile file(fileData);

    RiffReader reader;
    reader.Open(file.CPath());
    EXPECT_GT(reader.FileSize(), 0);
    reader.Close();
    EXPECT_EQ(reader.FileSize(), 0);
}

// ===========================================================================
// ReadChunkHeader
// ===========================================================================

class RiffReaderChunkTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Build a RIFF file with: LIST hdrl { 'avih'(8 bytes zero) }
        std::vector<uint8_t> avihData(56, 0); // MainAviHeader size
        auto avihChunk = MakeChunk(avi::kFourCC_avih, avihData);

        auto hdrlList = MakeListChunk(avi::kFourCC_hdrl, avihChunk);
        m_FileData = MakeRiffFile(avi::kFourCC_AVI, hdrlList);
        m_TempFile = std::make_unique<TempFile>(m_FileData);
        m_Reader.Open(m_TempFile->CPath());
    }

    void TearDown() override
    {
        m_Reader.Close(); // Close file handle before TempFile deletes the file
    }

    std::vector<uint8_t> m_FileData;
    std::unique_ptr<TempFile> m_TempFile; // must outlive m_Reader
    RiffReader m_Reader;
};

TEST_F(RiffReaderChunkTest, ReadsRiffHeader)
{
    RiffReader::Chunk riff;
    EXPECT_TRUE(m_Reader.ReadChunkHeader(riff));
    EXPECT_EQ(riff.id, avi::kFourCC_RIFF);
    EXPECT_EQ(riff.listType, avi::kFourCC_AVI);
    EXPECT_TRUE(riff.IsList());
    EXPECT_EQ(riff.dataOffset, 12); // past RIFF + size + formType
}

TEST_F(RiffReaderChunkTest, DescendIntoRiffAndReadChild)
{
    RiffReader::Chunk riff;
    ASSERT_TRUE(m_Reader.ReadChunkHeader(riff));
    ASSERT_TRUE(m_Reader.DescendInto(riff));

    RiffReader::Chunk child;
    ASSERT_TRUE(m_Reader.ReadChunkHeader(child));
    EXPECT_EQ(child.id, avi::kFourCC_LIST);
    EXPECT_EQ(child.listType, avi::kFourCC_hdrl);
}

TEST_F(RiffReaderChunkTest, DescendIntoListAndReadGrandchild)
{
    RiffReader::Chunk riff;
    ASSERT_TRUE(m_Reader.ReadChunkHeader(riff));
    ASSERT_TRUE(m_Reader.DescendInto(riff));

    RiffReader::Chunk hdrl;
    ASSERT_TRUE(m_Reader.ReadChunkHeader(hdrl));
    ASSERT_TRUE(m_Reader.DescendInto(hdrl));

    RiffReader::Chunk avih;
    ASSERT_TRUE(m_Reader.ReadChunkHeader(avih));
    EXPECT_EQ(avih.id, avi::kFourCC_avih);
    EXPECT_FALSE(avih.IsList());
    EXPECT_EQ(avih.size, 56u); // MainAviHeader is 56 bytes
}

TEST_F(RiffReaderChunkTest, AscendReturnsToParent)
{
    RiffReader::Chunk riff;
    ASSERT_TRUE(m_Reader.ReadChunkHeader(riff));
    ASSERT_TRUE(m_Reader.DescendInto(riff));

    RiffReader::Chunk hdrl;
    ASSERT_TRUE(m_Reader.ReadChunkHeader(hdrl));
    ASSERT_TRUE(m_Reader.DescendInto(hdrl));

    EXPECT_TRUE(m_Reader.Ascend());
    // After ascending from hdrl, we should be at the end of hdrl's data
    // No more children in the RIFF container
}

TEST_F(RiffReaderChunkTest, AscendAtTopLevelReturnsFalse)
{
    EXPECT_FALSE(m_Reader.Ascend());
}

TEST_F(RiffReaderChunkTest, DescendIntoNonListReturnsFalse)
{
    RiffReader::Chunk riff;
    ASSERT_TRUE(m_Reader.ReadChunkHeader(riff));
    ASSERT_TRUE(m_Reader.DescendInto(riff));

    RiffReader::Chunk hdrl;
    ASSERT_TRUE(m_Reader.ReadChunkHeader(hdrl));
    ASSERT_TRUE(m_Reader.DescendInto(hdrl));

    RiffReader::Chunk avih;
    ASSERT_TRUE(m_Reader.ReadChunkHeader(avih));
    EXPECT_FALSE(avih.IsList());
    EXPECT_FALSE(m_Reader.DescendInto(avih)); // not a list
}

// ===========================================================================
// SkipChunk
// ===========================================================================

TEST(RiffReaderSkip, OddSizeChunkPadded)
{
    // Build RIFF with a 3-byte chunk (odd size -> 1 pad byte) + a 4-byte chunk
    std::vector<uint8_t> inner;
    // First chunk: 3 bytes data
    auto chunk1 = MakeChunk(avi::MakeFourCC('T', 'S', 'T', '1'), {0xAA, 0xBB, 0xCC});
    // Second chunk: 4 bytes data
    auto chunk2 = MakeChunk(avi::MakeFourCC('T', 'S', 'T', '2'), {0x01, 0x02, 0x03, 0x04});
    AppendBytes(inner, chunk1.data(), chunk1.size());
    AppendBytes(inner, chunk2.data(), chunk2.size());

    auto fileData = MakeRiffFile(avi::kFourCC_AVI, inner);
    TempFile file(fileData);

    RiffReader reader;
    ASSERT_TRUE(reader.Open(file.CPath()));

    RiffReader::Chunk riff;
    ASSERT_TRUE(reader.ReadChunkHeader(riff));
    ASSERT_TRUE(reader.DescendInto(riff));

    RiffReader::Chunk c1;
    ASSERT_TRUE(reader.ReadChunkHeader(c1));
    EXPECT_EQ(c1.id, avi::MakeFourCC('T', 'S', 'T', '1'));
    EXPECT_EQ(c1.size, 3u);

    EXPECT_TRUE(reader.SkipChunk(c1));

    // Should now be positioned at TST2
    RiffReader::Chunk c2;
    ASSERT_TRUE(reader.ReadChunkHeader(c2));
    EXPECT_EQ(c2.id, avi::MakeFourCC('T', 'S', 'T', '2'));
    EXPECT_EQ(c2.size, 4u);
}

// ===========================================================================
// Read / ReadAt / SeekTo
// ===========================================================================

TEST(RiffReaderRead, ReadBytesCorrectly)
{
    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    auto chunk = MakeChunk(avi::MakeFourCC('D', 'A', 'T', 'A'), payload);
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, chunk);
    TempFile file(fileData);

    RiffReader reader;
    ASSERT_TRUE(reader.Open(file.CPath()));

    RiffReader::Chunk riff;
    ASSERT_TRUE(reader.ReadChunkHeader(riff));
    ASSERT_TRUE(reader.DescendInto(riff));

    RiffReader::Chunk data;
    ASSERT_TRUE(reader.ReadChunkHeader(data));

    uint8_t buf[4] = {};
    EXPECT_TRUE(reader.Read(buf, 4));
    EXPECT_EQ(buf[0], 0xDE);
    EXPECT_EQ(buf[1], 0xAD);
    EXPECT_EQ(buf[2], 0xBE);
    EXPECT_EQ(buf[3], 0xEF);
}

TEST(RiffReaderRead, ReadAtAbsoluteOffset)
{
    std::vector<uint8_t> payload = {0x11, 0x22, 0x33, 0x44};
    auto chunk = MakeChunk(avi::MakeFourCC('D', 'A', 'T', 'A'), payload);
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, chunk);
    TempFile file(fileData);

    RiffReader reader;
    ASSERT_TRUE(reader.Open(file.CPath()));

    // Data chunk header starts at offset 12 (after RIFF header).
    // Data payload starts at offset 12 + 8 = 20.
    uint8_t buf[2] = {};
    EXPECT_TRUE(reader.ReadAt(20, buf, 2));
    EXPECT_EQ(buf[0], 0x11);
    EXPECT_EQ(buf[1], 0x22);
}

TEST(RiffReaderRead, ReadAtPastEOFReturnsFalse)
{
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, {});
    TempFile file(fileData);

    RiffReader reader;
    ASSERT_TRUE(reader.Open(file.CPath()));

    uint8_t buf[4];
    EXPECT_FALSE(reader.ReadAt(10000, buf, 4));
}

TEST(RiffReaderSeek, NegativeOffsetFails)
{
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, {});
    TempFile file(fileData);

    RiffReader reader;
    ASSERT_TRUE(reader.Open(file.CPath()));
    EXPECT_FALSE(reader.SeekTo(-1));
}

TEST(RiffReaderSeek, PastEOFFails)
{
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, {});
    TempFile file(fileData);

    RiffReader reader;
    ASSERT_TRUE(reader.Open(file.CPath()));
    EXPECT_FALSE(reader.SeekTo(reader.FileSize() + 1));
}

TEST(RiffReaderSeek, SeekToZeroSucceeds)
{
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, {});
    TempFile file(fileData);

    RiffReader reader;
    ASSERT_TRUE(reader.Open(file.CPath()));
    EXPECT_TRUE(reader.SeekTo(0));
    EXPECT_EQ(reader.Tell(), 0);
}

// ===========================================================================
// Typed readers
// ===========================================================================

TEST(RiffReaderTyped, ReadU16LE)
{
    std::vector<uint8_t> payload = {0xCD, 0xAB};
    auto chunk = MakeChunk(avi::MakeFourCC('D', 'A', 'T', 'A'), payload);
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, chunk);
    TempFile file(fileData);

    RiffReader reader;
    ASSERT_TRUE(reader.Open(file.CPath()));
    ASSERT_TRUE(reader.SeekTo(20)); // jump to payload

    uint16_t v = 0;
    EXPECT_TRUE(reader.ReadU16(v));
    EXPECT_EQ(v, 0xABCDu);
}

TEST(RiffReaderTyped, ReadU32LE)
{
    std::vector<uint8_t> payload = {0x78, 0x56, 0x34, 0x12};
    auto chunk = MakeChunk(avi::MakeFourCC('D', 'A', 'T', 'A'), payload);
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, chunk);
    TempFile file(fileData);

    RiffReader reader;
    ASSERT_TRUE(reader.Open(file.CPath()));
    ASSERT_TRUE(reader.SeekTo(20));

    uint32_t v = 0;
    EXPECT_TRUE(reader.ReadU32(v));
    EXPECT_EQ(v, 0x12345678u);
}

TEST(RiffReaderTyped, ReadU64LE)
{
    std::vector<uint8_t> payload = {0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12};
    auto chunk = MakeChunk(avi::MakeFourCC('D', 'A', 'T', 'A'), payload);
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, chunk);
    TempFile file(fileData);

    RiffReader reader;
    ASSERT_TRUE(reader.Open(file.CPath()));
    ASSERT_TRUE(reader.SeekTo(20));

    uint64_t v = 0;
    EXPECT_TRUE(reader.ReadU64(v));
    EXPECT_EQ(v, 0x1234567890ABCDEFull);
}

TEST(RiffReaderTyped, ReadI32LE)
{
    // Encode -1 (0xFFFFFFFF) in LE.
    std::vector<uint8_t> payload = {0xFF, 0xFF, 0xFF, 0xFF};
    auto chunk = MakeChunk(avi::MakeFourCC('D', 'A', 'T', 'A'), payload);
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, chunk);
    TempFile file(fileData);

    RiffReader reader;
    ASSERT_TRUE(reader.Open(file.CPath()));
    ASSERT_TRUE(reader.SeekTo(20));

    int32_t v = 0;
    EXPECT_TRUE(reader.ReadI32(v));
    EXPECT_EQ(v, -1);
}

// ===========================================================================
// Tell
// ===========================================================================

TEST(RiffReaderTell, TellWithNoFileReturnsNegative)
{
    RiffReader reader;
    EXPECT_LT(reader.Tell(), 0);
}

TEST(RiffReaderTell, TellAfterOpenReturnsZero)
{
    auto fileData = MakeRiffFile(avi::kFourCC_JUNK, {});
    TempFile file(fileData);

    RiffReader reader;
    ASSERT_TRUE(reader.Open(file.CPath()));
    EXPECT_EQ(reader.Tell(), 0);
}
