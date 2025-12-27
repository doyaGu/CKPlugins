/**
 * @file TgaReaderTests.cpp
 * @brief Comprehensive TGA format tests for CKImageReader
 *
 * Tests cover:
 * - Uncompressed types (1, 2, 3): colormapped, truecolor, grayscale
 * - RLE compressed types (9, 10, 11)
 * - Various bit depths: 8, 15, 16, 24, 32
 * - Origin variations: top-left, top-right, bottom-left, bottom-right
 * - Colormap offset handling
 * - Save/load round-trips with optional RLE
 */

#include "TestFramework.h"
#include "TgaReader.h"
#include <cstring>
#include <algorithm>

using namespace TestFramework;

//=============================================================================
// Test Helpers
//=============================================================================

namespace {

struct TgaTestResult {
    int errorCode;
    uint32_t crc;
    int width;
    int height;
    int bytesPerLine;
    bool hasAlpha;

    TgaTestResult() : errorCode(-1), crc(0), width(0), height(0), bytesPerLine(0), hasAlpha(false) {}
};

TgaTestResult readTgaFile(const std::string& path) {
    TgaTestResult result;
    TgaReader reader;
    CKBitmapProperties* props = nullptr;

    result.errorCode = reader.ReadFile(const_cast<char*>(path.c_str()), &props);

    if (result.errorCode == 0 && props) {
        result.width = props->m_Format.Width;
        result.height = props->m_Format.Height;
        result.bytesPerLine = props->m_Format.BytesPerLine;
        result.hasAlpha = (props->m_Format.AlphaMask != 0);

        if (props->m_Format.Image && result.height > 0 && result.bytesPerLine > 0) {
            size_t imageSize = static_cast<size_t>(result.bytesPerLine) * result.height;
            result.crc = CRC32::compute(props->m_Format.Image, imageSize);
        }

        ImageReader::FreeBitmapData(props);
    }

    return result;
}

TgaTestResult readTgaMemory(const void* data, int size) {
    TgaTestResult result;
    TgaReader reader;
    CKBitmapProperties* props = nullptr;

    result.errorCode = reader.ReadMemory(const_cast<void*>(data), size, &props);

    if (result.errorCode == 0 && props) {
        result.width = props->m_Format.Width;
        result.height = props->m_Format.Height;
        result.bytesPerLine = props->m_Format.BytesPerLine;
        result.hasAlpha = (props->m_Format.AlphaMask != 0);

        if (props->m_Format.Image && result.height > 0 && result.bytesPerLine > 0) {
            size_t imageSize = static_cast<size_t>(result.bytesPerLine) * result.height;
            result.crc = CRC32::compute(props->m_Format.Image, imageSize);
        }

        ImageReader::FreeBitmapData(props);
    }

    return result;
}

std::string getTgaTestImagePath(const std::string& subdir, const std::string& filename) {
    return joinPath(joinPath(joinPath(g_TestImagesDir, "tga"), subdir), filename);
}

std::string getTgaReferencePath(const std::string& subdir, const std::string& filename) {
    return joinPath(joinPath(joinPath(g_TestReferenceDir, "tga"), subdir), filename);
}

bool findTgaExpectedCrc(const std::string& subdir, const std::string& inputName, uint32_t& outCrc) {
    // First, try CKImageReader-specific CRCs (preferred)
    std::string key = "tga/" + subdir + "/" + inputName;
    if (getReferenceCrc(key, outCrc)) {
        return true;
    }

    // Fall back to external reference files
    std::string refDir = joinPath(joinPath(g_TestReferenceDir, "tga"), subdir);
    std::vector<std::string> files = listDirectory(refDir);

    for (size_t i = 0; i < files.size(); ++i) {
        ReferenceInfo info = parseReferenceFilename(files[i]);
        if (info.valid && info.inputName == inputName) {
            outCrc = info.expectedCrc;
            return true;
        }
    }
    return false;
}

} // anonymous namespace

//=============================================================================
// Testsuite - Basic Format Tests
//=============================================================================

TEST(TgaReader, Uncompressed_BlackWhite_8bit) {
    std::string path = getTgaTestImagePath("testsuite", "ubw8.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);
    ASSERT_TRUE(result.width > 0);
    ASSERT_TRUE(result.height > 0);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "ubw8.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

TEST(TgaReader, Compressed_BlackWhite_8bit) {
    std::string path = getTgaTestImagePath("testsuite", "cbw8.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "cbw8.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

TEST(TgaReader, Uncompressed_ColorMap_8bit) {
    std::string path = getTgaTestImagePath("testsuite", "ucm8.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);
    // ucm8 may not have a reference, just verify load
}

TEST(TgaReader, Compressed_ColorMap_8bit) {
    std::string path = getTgaTestImagePath("testsuite", "ccm8.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);
}

TEST(TgaReader, Uncompressed_TrueColor_16bit) {
    std::string path = getTgaTestImagePath("testsuite", "utc16.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);
}

TEST(TgaReader, Uncompressed_TrueColor_24bit) {
    std::string path = getTgaTestImagePath("testsuite", "utc24.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "utc24.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

TEST(TgaReader, Compressed_TrueColor_24bit) {
    std::string path = getTgaTestImagePath("testsuite", "ctc24.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "ctc24.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

TEST(TgaReader, Uncompressed_TrueColor_32bit) {
    std::string path = getTgaTestImagePath("testsuite", "utc32.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);
    ASSERT_TRUE(result.hasAlpha);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "utc32.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

//=============================================================================
// Origin Tests
//=============================================================================

TEST(TgaReader, Origin_BottomLeft) {
    std::string path = getTgaTestImagePath("testsuite", "bottom_left.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "bottom_left.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

TEST(TgaReader, Origin_BottomRight) {
    std::string path = getTgaTestImagePath("testsuite", "bottom_right.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "bottom_right.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

TEST(TgaReader, Origin_TopLeft) {
    std::string path = getTgaTestImagePath("testsuite", "top_left.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "top_left.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

TEST(TgaReader, Origin_TopRight) {
    std::string path = getTgaTestImagePath("testsuite", "top_right.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "top_right.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

//=============================================================================
// 16-bit Attribute Tests
//=============================================================================

TEST(TgaReader, B5_WithAttrib) {
    std::string path = getTgaTestImagePath("testsuite", "b5-attrib.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "b5-attrib.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

TEST(TgaReader, B5_NoAttrib) {
    std::string path = getTgaTestImagePath("testsuite", "b5-noattrib.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "b5-noattrib.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

TEST(TgaReader, B5_ColorMap) {
    std::string path = getTgaTestImagePath("testsuite", "b5-cmap.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "b5-cmap.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

TEST(TgaReader, B5_UnusedColorMap) {
    std::string path = getTgaTestImagePath("testsuite", "b5-unused-cmap.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "b5-unused-cmap.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

//=============================================================================
// Colormap Offset Test
//=============================================================================

TEST(TgaReader, ColorMap_Offset) {
    std::string path = getTgaTestImagePath("testsuite", "cmap_offset.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "cmap_offset.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

//=============================================================================
// Encoding Tests
//=============================================================================

TEST(TgaReader, Encoding_BlackWhite) {
    std::string path = getTgaTestImagePath("encoding", "black_white.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    TgaTestResult result = readTgaFile(path);
    ASSERT_EQ(0, result.errorCode);
}

//=============================================================================
// Memory Read Tests
//=============================================================================

TEST(TgaReader, ReadMemory_TrueColor24) {
    std::string path = getTgaTestImagePath("testsuite", "utc24.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    std::vector<uint8_t> data = readBinaryFile(path);
    ASSERT_TRUE(!data.empty());

    TgaTestResult result = readTgaMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_EQ(0, result.errorCode);

    uint32_t expectedCrc;
    if (findTgaExpectedCrc("testsuite", "utc24.tga", expectedCrc)) {
        ASSERT_EQ(expectedCrc, result.crc);
    }
}

TEST(TgaReader, ReadMemory_TrueColor32) {
    std::string path = getTgaTestImagePath("testsuite", "utc32.tga");
    if (!fileExists(path)) SKIP_TEST("Test image not found");

    std::vector<uint8_t> data = readBinaryFile(path);
    ASSERT_TRUE(!data.empty());

    TgaTestResult result = readTgaMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_TRUE(result.hasAlpha);
}

//=============================================================================
// Save/Load Round-trip Tests
//=============================================================================

TEST(TgaReader, SaveLoad_24bit_NoRLE) {
    std::string inputPath = getTgaTestImagePath("testsuite", "utc24.tga");
    if (!fileExists(inputPath)) SKIP_TEST("Test image not found");

    // Read original
    TgaReader reader1;
    CKBitmapProperties* props1 = nullptr;
    int err = reader1.ReadFile(const_cast<char*>(inputPath.c_str()), &props1);
    ASSERT_EQ(0, err);
    ASSERT_TRUE(props1 != nullptr);

    size_t imageSize = static_cast<size_t>(props1->m_Format.BytesPerLine) * props1->m_Format.Height;
    uint32_t originalCrc = CRC32::compute(props1->m_Format.Image, imageSize);

    // Configure for 24-bit, no RLE
    TgaBitmapProperties* tgaProps = static_cast<TgaBitmapProperties*>(props1);
    tgaProps->m_BitDepth = 24;
    tgaProps->m_UseRLE = 0;

    std::string outputPath = joinPath(g_TestOutputDir, "roundtrip_tga24.tga");
    TgaReader reader2;
    err = reader2.SaveFile(const_cast<char*>(outputPath.c_str()), props1);
    ASSERT_TRUE(err > 0);  // SaveFile returns file size on success

    ImageReader::FreeBitmapData(props1);

    // Read back
    TgaReader reader3;
    CKBitmapProperties* props3 = nullptr;
    err = reader3.ReadFile(const_cast<char*>(outputPath.c_str()), &props3);
    ASSERT_EQ(0, err);
    ASSERT_TRUE(props3 != nullptr);

    size_t imageSize3 = static_cast<size_t>(props3->m_Format.BytesPerLine) * props3->m_Format.Height;
    uint32_t roundtripCrc = CRC32::compute(props3->m_Format.Image, imageSize3);

    ASSERT_EQ(originalCrc, roundtripCrc);

    ImageReader::FreeBitmapData(props3);
}

TEST(TgaReader, SaveLoad_24bit_RLE) {
    std::string inputPath = getTgaTestImagePath("testsuite", "utc24.tga");
    if (!fileExists(inputPath)) SKIP_TEST("Test image not found");

    TgaReader reader1;
    CKBitmapProperties* props1 = nullptr;
    int err = reader1.ReadFile(const_cast<char*>(inputPath.c_str()), &props1);
    ASSERT_EQ(0, err);
    ASSERT_TRUE(props1 != nullptr);

    size_t imageSize = static_cast<size_t>(props1->m_Format.BytesPerLine) * props1->m_Format.Height;
    uint32_t originalCrc = CRC32::compute(props1->m_Format.Image, imageSize);

    // Configure for 24-bit with RLE
    TgaBitmapProperties* tgaProps = static_cast<TgaBitmapProperties*>(props1);
    tgaProps->m_BitDepth = 24;
    tgaProps->m_UseRLE = 1;

    std::string outputPath = joinPath(g_TestOutputDir, "roundtrip_tga24_rle.tga");
    TgaReader reader2;
    err = reader2.SaveFile(const_cast<char*>(outputPath.c_str()), props1);
    ASSERT_TRUE(err > 0);  // SaveFile returns file size on success

    ImageReader::FreeBitmapData(props1);

    // Read back
    TgaReader reader3;
    CKBitmapProperties* props3 = nullptr;
    err = reader3.ReadFile(const_cast<char*>(outputPath.c_str()), &props3);
    ASSERT_EQ(0, err);
    ASSERT_TRUE(props3 != nullptr);

    size_t imageSize3 = static_cast<size_t>(props3->m_Format.BytesPerLine) * props3->m_Format.Height;
    uint32_t roundtripCrc = CRC32::compute(props3->m_Format.Image, imageSize3);

    ASSERT_EQ(originalCrc, roundtripCrc);

    ImageReader::FreeBitmapData(props3);
}

TEST(TgaReader, SaveLoad_32bit_NoRLE) {
    std::string inputPath = getTgaTestImagePath("testsuite", "utc32.tga");
    if (!fileExists(inputPath)) SKIP_TEST("Test image not found");

    TgaReader reader1;
    CKBitmapProperties* props1 = nullptr;
    int err = reader1.ReadFile(const_cast<char*>(inputPath.c_str()), &props1);
    ASSERT_EQ(0, err);
    ASSERT_TRUE(props1 != nullptr);

    size_t imageSize = static_cast<size_t>(props1->m_Format.BytesPerLine) * props1->m_Format.Height;
    uint32_t originalCrc = CRC32::compute(props1->m_Format.Image, imageSize);

    // Configure for 32-bit, no RLE
    TgaBitmapProperties* tgaProps = static_cast<TgaBitmapProperties*>(props1);
    tgaProps->m_BitDepth = 32;
    tgaProps->m_UseRLE = 0;

    std::string outputPath = joinPath(g_TestOutputDir, "roundtrip_tga32.tga");
    TgaReader reader2;
    err = reader2.SaveFile(const_cast<char*>(outputPath.c_str()), props1);
    ASSERT_TRUE(err > 0);  // SaveFile returns file size on success

    ImageReader::FreeBitmapData(props1);

    // Read back
    TgaReader reader3;
    CKBitmapProperties* props3 = nullptr;
    err = reader3.ReadFile(const_cast<char*>(outputPath.c_str()), &props3);
    ASSERT_EQ(0, err);
    ASSERT_TRUE(props3 != nullptr);

    size_t imageSize3 = static_cast<size_t>(props3->m_Format.BytesPerLine) * props3->m_Format.Height;
    uint32_t roundtripCrc = CRC32::compute(props3->m_Format.Image, imageSize3);

    ASSERT_EQ(originalCrc, roundtripCrc);

    ImageReader::FreeBitmapData(props3);
}

TEST(TgaReader, SaveLoad_32bit_RLE) {
    std::string inputPath = getTgaTestImagePath("testsuite", "utc32.tga");
    if (!fileExists(inputPath)) SKIP_TEST("Test image not found");

    TgaReader reader1;
    CKBitmapProperties* props1 = nullptr;
    int err = reader1.ReadFile(const_cast<char*>(inputPath.c_str()), &props1);
    ASSERT_EQ(0, err);
    ASSERT_TRUE(props1 != nullptr);

    size_t imageSize = static_cast<size_t>(props1->m_Format.BytesPerLine) * props1->m_Format.Height;
    uint32_t originalCrc = CRC32::compute(props1->m_Format.Image, imageSize);

    // Configure for 32-bit with RLE
    TgaBitmapProperties* tgaProps = static_cast<TgaBitmapProperties*>(props1);
    tgaProps->m_BitDepth = 32;
    tgaProps->m_UseRLE = 1;

    std::string outputPath = joinPath(g_TestOutputDir, "roundtrip_tga32_rle.tga");
    TgaReader reader2;
    err = reader2.SaveFile(const_cast<char*>(outputPath.c_str()), props1);
    ASSERT_TRUE(err > 0);  // SaveFile returns file size on success

    ImageReader::FreeBitmapData(props1);

    // Read back
    TgaReader reader3;
    CKBitmapProperties* props3 = nullptr;
    err = reader3.ReadFile(const_cast<char*>(outputPath.c_str()), &props3);
    ASSERT_EQ(0, err);
    ASSERT_TRUE(props3 != nullptr);

    size_t imageSize3 = static_cast<size_t>(props3->m_Format.BytesPerLine) * props3->m_Format.Height;
    uint32_t roundtripCrc = CRC32::compute(props3->m_Format.Image, imageSize3);

    ASSERT_EQ(originalCrc, roundtripCrc);

    ImageReader::FreeBitmapData(props3);
}

//=============================================================================
// API Tests
//=============================================================================

TEST(TgaReader, GetReaderInfo) {
    TgaReader reader;
    CKPluginInfo* info = reader.GetReaderInfo();
    ASSERT_TRUE(info != nullptr);
    ASSERT_GUID_EQ(TGAREADER_GUID, info->m_GUID);
}

TEST(TgaReader, GetOptionsCount) {
    TgaReader reader;
    int count = reader.GetOptionsCount();
    ASSERT_EQ(2, count); // Bit depth + RLE
}

TEST(TgaReader, GetOptionDescription_BitDepth) {
    TgaReader reader;
    CKSTRING desc = reader.GetOptionDescription(0);
    ASSERT_TRUE(desc != nullptr);
    ASSERT_TRUE(strstr(desc, "Bit Depth") != nullptr);
}

TEST(TgaReader, GetOptionDescription_RLE) {
    TgaReader reader;
    CKSTRING desc = reader.GetOptionDescription(1);
    ASSERT_TRUE(desc != nullptr);
    ASSERT_TRUE(strstr(desc, "Run Length") != nullptr || strstr(desc, "RLE") != nullptr);
}

TEST(TgaReader, GetFlags) {
    TgaReader reader;
    CK_DATAREADER_FLAGS flags = reader.GetFlags();
    ASSERT_EQ(15, flags);
}

TEST(TgaReader, IsAlphaSaved_24bit) {
    TgaReader reader;
    TgaBitmapProperties props;
    props.m_BitDepth = 24;
    ASSERT_FALSE(reader.IsAlphaSaved(&props));
}

TEST(TgaReader, IsAlphaSaved_32bit) {
    TgaReader reader;
    TgaBitmapProperties props;
    props.m_BitDepth = 32;
    ASSERT_TRUE(reader.IsAlphaSaved(&props));
}

//=============================================================================
// Additional Generated TGA Tests
//=============================================================================

namespace {

std::vector<uint8_t> generateTgaUncompressed24(int width, int height, uint8_t origin = 0x20) {
    std::vector<uint8_t> data;
    
    // TGA header - 18 bytes
    data.push_back(0);       // ID length
    data.push_back(0);       // Colormap type (none)
    data.push_back(2);       // Image type (uncompressed true-color)
    // Colormap spec - 5 bytes
    data.push_back(0); data.push_back(0);  // First entry index
    data.push_back(0); data.push_back(0);  // Colormap length
    data.push_back(0);       // Colormap entry size
    // Image spec - 10 bytes
    data.push_back(0); data.push_back(0);  // X origin
    data.push_back(0); data.push_back(0);  // Y origin
    data.push_back(static_cast<uint8_t>(width & 0xFF));
    data.push_back(static_cast<uint8_t>((width >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(height & 0xFF));
    data.push_back(static_cast<uint8_t>((height >> 8) & 0xFF));
    data.push_back(24);      // Pixel depth
    data.push_back(origin);  // Image descriptor (origin)
    
    // Pixel data
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data.push_back(static_cast<uint8_t>((x + y) % 256)); // B
            data.push_back(static_cast<uint8_t>((x * 2) % 256)); // G
            data.push_back(static_cast<uint8_t>((y * 2) % 256)); // R
        }
    }
    
    return data;
}

std::vector<uint8_t> generateTgaUncompressed32(int width, int height, uint8_t origin = 0x28) {
    std::vector<uint8_t> data;
    
    data.push_back(0);       // ID length
    data.push_back(0);       // Colormap type
    data.push_back(2);       // Image type
    data.push_back(0); data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(static_cast<uint8_t>(width & 0xFF));
    data.push_back(static_cast<uint8_t>((width >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(height & 0xFF));
    data.push_back(static_cast<uint8_t>((height >> 8) & 0xFF));
    data.push_back(32);      // Pixel depth
    data.push_back(origin);  // Image descriptor
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data.push_back(static_cast<uint8_t>((x + y) % 256));
            data.push_back(static_cast<uint8_t>((x * 2) % 256));
            data.push_back(static_cast<uint8_t>((y * 2) % 256));
            data.push_back(static_cast<uint8_t>(255 - (x * y) % 256)); // A
        }
    }
    
    return data;
}

std::vector<uint8_t> generateTgaGrayscale(int width, int height) {
    std::vector<uint8_t> data;
    
    data.push_back(0);       // ID length
    data.push_back(0);       // Colormap type
    data.push_back(3);       // Image type (uncompressed grayscale)
    data.push_back(0); data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(static_cast<uint8_t>(width & 0xFF));
    data.push_back(static_cast<uint8_t>((width >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(height & 0xFF));
    data.push_back(static_cast<uint8_t>((height >> 8) & 0xFF));
    data.push_back(8);       // Pixel depth
    data.push_back(0x20);    // Top-left origin
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data.push_back(static_cast<uint8_t>((x + y * 3) % 256));
        }
    }
    
    return data;
}

std::vector<uint8_t> generateTgaColormapped(int width, int height) {
    std::vector<uint8_t> data;
    
    data.push_back(0);       // ID length
    data.push_back(1);       // Colormap type (has colormap)
    data.push_back(1);       // Image type (uncompressed colormapped)
    // Colormap spec
    data.push_back(0); data.push_back(0);  // First entry index
    data.push_back(0); data.push_back(1);  // Colormap length = 256
    data.push_back(24);      // Colormap entry size
    // Image spec
    data.push_back(0); data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(static_cast<uint8_t>(width & 0xFF));
    data.push_back(static_cast<uint8_t>((width >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(height & 0xFF));
    data.push_back(static_cast<uint8_t>((height >> 8) & 0xFF));
    data.push_back(8);       // Pixel depth (index)
    data.push_back(0x20);    // Top-left origin
    
    // Colormap (256 entries, 24-bit)
    for (int i = 0; i < 256; ++i) {
        data.push_back(static_cast<uint8_t>(i));       // B
        data.push_back(static_cast<uint8_t>(i / 2));   // G
        data.push_back(static_cast<uint8_t>(255 - i)); // R
    }
    
    // Pixel indices
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            data.push_back(static_cast<uint8_t>((x + y * 2) % 256));
        }
    }
    
    return data;
}

std::vector<uint8_t> generateTgaRLE24(int width, int height) {
    std::vector<uint8_t> data;
    
    data.push_back(0);       // ID length
    data.push_back(0);       // Colormap type
    data.push_back(10);      // Image type (RLE true-color)
    data.push_back(0); data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(static_cast<uint8_t>(width & 0xFF));
    data.push_back(static_cast<uint8_t>((width >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(height & 0xFF));
    data.push_back(static_cast<uint8_t>((height >> 8) & 0xFF));
    data.push_back(24);      // Pixel depth
    data.push_back(0x20);    // Top-left origin
    
    // Encode each row with RLE
    for (int y = 0; y < height; ++y) {
        int x = 0;
        while (x < width) {
            // Alternate between runs and literals
            if ((y + x) % 2 == 0 && x + 4 <= width) {
                // Run packet
                int runLen = (std::min)(4, width - x);
                data.push_back(static_cast<uint8_t>(0x80 | (runLen - 1)));
                data.push_back(static_cast<uint8_t>((x + y) % 256));
                data.push_back(static_cast<uint8_t>((x * 2) % 256));
                data.push_back(static_cast<uint8_t>((y * 2) % 256));
                x += runLen;
            } else {
                // Literal packet
                int litLen = (std::min)(3, width - x);
                data.push_back(static_cast<uint8_t>(litLen - 1));
                for (int i = 0; i < litLen; ++i) {
                    data.push_back(static_cast<uint8_t>((x + i + y) % 256));
                    data.push_back(static_cast<uint8_t>(((x + i) * 2) % 256));
                    data.push_back(static_cast<uint8_t>((y * 2) % 256));
                }
                x += litLen;
            }
        }
    }
    
    return data;
}

std::vector<uint8_t> generateTga16bit(int width, int height) {
    std::vector<uint8_t> data;
    
    data.push_back(0);       // ID length
    data.push_back(0);       // Colormap type
    data.push_back(2);       // Image type (uncompressed true-color)
    data.push_back(0); data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(0); data.push_back(0);
    data.push_back(static_cast<uint8_t>(width & 0xFF));
    data.push_back(static_cast<uint8_t>((width >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(height & 0xFF));
    data.push_back(static_cast<uint8_t>((height >> 8) & 0xFF));
    data.push_back(16);      // Pixel depth (15/16-bit)
    data.push_back(0x20);    // Top-left origin
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // 16-bit: ARRRRRGG GGGBBBBB (1-5-5-5)
            uint8_t r = (x * 2) % 32;
            uint8_t g = (y * 2) % 32;
            uint8_t b = ((x + y) * 2) % 32;
            uint16_t pixel = (1 << 15) | (r << 10) | (g << 5) | b;
            data.push_back(static_cast<uint8_t>(pixel & 0xFF));
            data.push_back(static_cast<uint8_t>((pixel >> 8) & 0xFF));
        }
    }
    
    return data;
}

} // anonymous namespace

TEST(TgaReader, Generated_1x1_24bit) {
    std::vector<uint8_t> tga = generateTgaUncompressed24(1, 1);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(1, result.width);
    ASSERT_EQ(1, result.height);
}

TEST(TgaReader, Generated_1x1_32bit) {
    std::vector<uint8_t> tga = generateTgaUncompressed32(1, 1);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(1, result.width);
    ASSERT_EQ(1, result.height);
}

TEST(TgaReader, Generated_1x1_Grayscale) {
    std::vector<uint8_t> tga = generateTgaGrayscale(1, 1);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(1, result.width);
    ASSERT_EQ(1, result.height);
}

TEST(TgaReader, Generated_2x2_24bit) {
    std::vector<uint8_t> tga = generateTgaUncompressed24(2, 2);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(2, result.width);
    ASSERT_EQ(2, result.height);
}

TEST(TgaReader, Generated_16x16_24bit) {
    std::vector<uint8_t> tga = generateTgaUncompressed24(16, 16);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(16, result.width);
    ASSERT_EQ(16, result.height);
}

TEST(TgaReader, Generated_100x100_32bit) {
    std::vector<uint8_t> tga = generateTgaUncompressed32(100, 100);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(100, result.width);
    ASSERT_EQ(100, result.height);
}

TEST(TgaReader, Generated_256x256_Grayscale) {
    std::vector<uint8_t> tga = generateTgaGrayscale(256, 256);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(256, result.width);
    ASSERT_EQ(256, result.height);
}

TEST(TgaReader, Generated_64x64_Colormapped) {
    std::vector<uint8_t> tga = generateTgaColormapped(64, 64);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(64, result.width);
    ASSERT_EQ(64, result.height);
}

TEST(TgaReader, Generated_32x32_RLE24) {
    std::vector<uint8_t> tga = generateTgaRLE24(32, 32);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(32, result.width);
    ASSERT_EQ(32, result.height);
}

TEST(TgaReader, Generated_64x64_16bit) {
    std::vector<uint8_t> tga = generateTga16bit(64, 64);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(64, result.width);
    ASSERT_EQ(64, result.height);
}

TEST(TgaReader, Generated_BottomLeft_Origin) {
    std::vector<uint8_t> tga = generateTgaUncompressed24(32, 32, 0x00); // Bottom-left
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(32, result.width);
    ASSERT_EQ(32, result.height);
}

TEST(TgaReader, Generated_BottomRight_Origin) {
    std::vector<uint8_t> tga = generateTgaUncompressed24(32, 32, 0x10); // Bottom-right
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(32, result.width);
    ASSERT_EQ(32, result.height);
}

TEST(TgaReader, Generated_TopRight_Origin) {
    std::vector<uint8_t> tga = generateTgaUncompressed24(32, 32, 0x30); // Top-right
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(32, result.width);
    ASSERT_EQ(32, result.height);
}

TEST(TgaReader, Generated_WideImage_512x8) {
    std::vector<uint8_t> tga = generateTgaUncompressed24(512, 8);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(512, result.width);
    ASSERT_EQ(8, result.height);
}

TEST(TgaReader, Generated_TallImage_8x512) {
    std::vector<uint8_t> tga = generateTgaUncompressed24(8, 512);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(8, result.width);
    ASSERT_EQ(512, result.height);
}

TEST(TgaReader, Generated_NonPow2_37x53) {
    std::vector<uint8_t> tga = generateTgaUncompressed24(37, 53);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(37, result.width);
    ASSERT_EQ(53, result.height);
}

TEST(TgaReader, Generated_PrimeSize_127x131) {
    std::vector<uint8_t> tga = generateTgaUncompressed24(127, 131);
    TgaTestResult result = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(127, result.width);
    ASSERT_EQ(131, result.height);
}

//=============================================================================
// Additional Negative Tests
//=============================================================================

TEST(TgaReader, Negative_TruncatedHeader) {
    std::vector<uint8_t> data = generateTgaUncompressed24(4, 4);
    data.resize(10);
    TgaTestResult result = readTgaMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(TgaReader, Negative_TruncatedPixelData) {
    std::vector<uint8_t> data = generateTgaUncompressed24(32, 32);
    data.resize(18 + 100);  // Header + partial pixels
    TgaTestResult result = readTgaMemory(data.data(), static_cast<int>(data.size()));
    // Reader may either reject or partially decode - test for no crash
    ASSERT_TRUE(result.errorCode != 0 || result.width == 32);
}

TEST(TgaReader, Negative_TruncatedColormap) {
    std::vector<uint8_t> data = generateTgaColormapped(16, 16);
    data.resize(18 + 100);  // Header + partial colormap
    TgaTestResult result = readTgaMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(TgaReader, Truncated_File) {
    std::string path = getTgaTestImagePath("testsuite", "utc24.tga");
    if (!fileExists(path)) {
        std::vector<uint8_t> tga = generateTgaUncompressed24(16, 16);
        TgaTestResult result = readTgaMemory(tga.data(), 18);
        ASSERT_NE(0, result.errorCode);
        return;
    }

    std::vector<uint8_t> data = readBinaryFile(path);
    ASSERT_TRUE(data.size() > 100);

    // Truncate to just header
    TgaTestResult result = readTgaMemory(data.data(), 18);
    ASSERT_NE(0, result.errorCode);
}

TEST(TgaReader, Empty_File) {
    uint8_t emptyData[1] = {0};
    TgaTestResult result = readTgaMemory(emptyData, 0);
    ASSERT_NE(0, result.errorCode);
}

TEST(TgaReader, Invalid_ImageType) {
    std::vector<uint8_t> data = generateTgaUncompressed24(16, 16);
    // Corrupt image type byte (offset 2)
    data[2] = 99; // Invalid type
    TgaTestResult result = readTgaMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(TgaReader, Negative_ZeroWidth) {
    std::vector<uint8_t> data = generateTgaUncompressed24(4, 4);
    data[12] = 0; data[13] = 0; // Width = 0
    TgaTestResult result = readTgaMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(TgaReader, Negative_ZeroHeight) {
    std::vector<uint8_t> data = generateTgaUncompressed24(4, 4);
    data[14] = 0; data[15] = 0; // Height = 0
    TgaTestResult result = readTgaMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(TgaReader, Negative_InvalidPixelDepth) {
    std::vector<uint8_t> data = generateTgaUncompressed24(4, 4);
    data[16] = 7; // Invalid pixel depth
    TgaTestResult result = readTgaMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(TgaReader, Negative_EmptyData) {
    TgaTestResult result = readTgaMemory(nullptr, 0);
    ASSERT_NE(0, result.errorCode);
}

TEST(TgaReader, Negative_OneByte) {
    uint8_t data = 0;
    TgaTestResult result = readTgaMemory(&data, 1);
    ASSERT_NE(0, result.errorCode);
}

TEST(TgaReader, Negative_VeryLargeDimensions) {
    std::vector<uint8_t> data = generateTgaUncompressed24(4, 4);
    // Set dimensions to max
    data[12] = 0xFF; data[13] = 0xFF; // Width = 65535
    data[14] = 0xFF; data[15] = 0xFF; // Height = 65535
    TgaTestResult result = readTgaMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_NE(0, result.errorCode);
}

//=============================================================================
// Memory vs File Consistency Tests
//=============================================================================

TEST(TgaReader, MemoryFileConsistency_24bit) {
    std::vector<uint8_t> tga = generateTgaUncompressed24(32, 32);
    
    TgaTestResult memResult = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, memResult.errorCode);
    
    std::string tempPath = joinPath(g_TestOutputDir, "tga_consistency_test.tga");
    FILE* f = fopen(tempPath.c_str(), "wb");
    if (f) {
        fwrite(tga.data(), 1, tga.size(), f);
        fclose(f);
        
        TgaTestResult fileResult = readTgaFile(tempPath);
        ASSERT_EQ(0, fileResult.errorCode);
        ASSERT_EQ(memResult.width, fileResult.width);
        ASSERT_EQ(memResult.height, fileResult.height);
        ASSERT_EQ(memResult.crc, fileResult.crc);
    }
}

TEST(TgaReader, MemoryFileConsistency_Grayscale) {
    std::vector<uint8_t> tga = generateTgaGrayscale(64, 64);
    
    TgaTestResult memResult = readTgaMemory(tga.data(), static_cast<int>(tga.size()));
    ASSERT_EQ(0, memResult.errorCode);
    
    std::string tempPath = joinPath(g_TestOutputDir, "tga_consistency_gray.tga");
    FILE* f = fopen(tempPath.c_str(), "wb");
    if (f) {
        fwrite(tga.data(), 1, tga.size(), f);
        fclose(f);
        
        TgaTestResult fileResult = readTgaFile(tempPath);
        ASSERT_EQ(0, fileResult.errorCode);
        ASSERT_EQ(memResult.crc, fileResult.crc);
    }
}

//=============================================================================
// Additional API Tests
//=============================================================================

TEST(TgaReader, MultipleInstancesIndependent) {
    TgaReader reader1;
    TgaReader reader2;
    
    std::vector<uint8_t> tga1 = generateTgaUncompressed24(16, 16);
    std::vector<uint8_t> tga2 = generateTgaUncompressed24(32, 32);
    
    TgaTestResult result1 = readTgaMemory(tga1.data(), static_cast<int>(tga1.size()));
    TgaTestResult result2 = readTgaMemory(tga2.data(), static_cast<int>(tga2.size()));
    
    ASSERT_EQ(0, result1.errorCode);
    ASSERT_EQ(0, result2.errorCode);
    ASSERT_EQ(16, result1.width);
    ASSERT_EQ(32, result2.width);
}

TEST(TgaReader, ReaderReuse) {
    TgaReader reader;
    std::vector<uint8_t> tga1 = generateTgaUncompressed24(8, 8);
    std::vector<uint8_t> tga2 = generateTgaUncompressed24(16, 16);
    
    CKBitmapProperties* props1 = nullptr;
    int err1 = reader.ReadMemory(tga1.data(), static_cast<int>(tga1.size()), &props1);
    ASSERT_EQ(0, err1);
    if (props1) {
        ASSERT_EQ(8, props1->m_Format.Width);
        ImageReader::FreeBitmapData(props1);
    }
    
    CKBitmapProperties* props2 = nullptr;
    int err2 = reader.ReadMemory(tga2.data(), static_cast<int>(tga2.size()), &props2);
    ASSERT_EQ(0, err2);
    if (props2) {
        ASSERT_EQ(16, props2->m_Format.Width);
        ImageReader::FreeBitmapData(props2);
    }
}

//=============================================================================
// Corpus Tests - Iterate ALL TGA Fixtures
// These tests ensure every fixture file in tests/images/tga is exercised
//=============================================================================

TEST(TgaReader, AllTgaTestsuite_MustDecode) {
    // Test all .tga files in testsuite subdirectory
    std::string tgaDir = joinPath(joinPath(g_TestImagesDir, "tga"), "testsuite");
    if (!directoryExists(tgaDir)) SKIP_TEST("TGA testsuite directory not found");
    
    std::vector<std::string> tgaFiles = collectFilesWithExtension(tgaDir, ".tga");
    if (tgaFiles.empty()) SKIP_TEST("No TGA files found in testsuite");
    
    CorpusTestStats stats;
    std::vector<std::string> missingCrcs;
    
    for (size_t i = 0; i < tgaFiles.size(); ++i) {
        const std::string& filename = tgaFiles[i];
        std::string filepath = joinPath(tgaDir, filename);
        
        TgaTestResult result = readTgaFile(filepath);
        
        if (result.errorCode != 0) {
            stats.recordFail(filename, "decode failed with error " + std::to_string(result.errorCode));
            continue;
        }
        
        if (result.width <= 0 || result.height <= 0) {
            stats.recordFail(filename, "invalid dimensions");
            continue;
        }
        
        // Check CRC if available
        uint32_t expectedCrc;
        if (findTgaExpectedCrc("testsuite", filename, expectedCrc)) {
            if (result.crc != expectedCrc) {
                std::ostringstream oss;
                oss << "CRC mismatch: expected " << std::hex << expectedCrc 
                    << " got " << result.crc;
                stats.recordFail(filename, oss.str());
                continue;
            }
        } else {
            missingCrcs.push_back(filename);
        }
        
        stats.recordPass();
    }
    
    if (!missingCrcs.empty()) {
        printf("    NOTE: %zu testsuite files have no reference CRC\n", missingCrcs.size());
    }
    
    printf("    %s\n", stats.summary().c_str());
    
    if (!stats.allPassed()) {
        for (size_t i = 0; i < stats.failures.size(); ++i) {
            printf("      FAIL: %s\n", stats.failures[i].c_str());
        }
        ASSERT_TRUE(false);
    }
}

TEST(TgaReader, AllTgaEncoding_MustDecode) {
    // Test all .tga files in encoding subdirectory
    std::string tgaDir = joinPath(joinPath(g_TestImagesDir, "tga"), "encoding");
    if (!directoryExists(tgaDir)) SKIP_TEST("TGA encoding directory not found");
    
    std::vector<std::string> tgaFiles = collectFilesWithExtension(tgaDir, ".tga");
    if (tgaFiles.empty()) SKIP_TEST("No TGA files found in encoding");
    
    CorpusTestStats stats;
    std::vector<std::string> missingCrcs;
    
    for (size_t i = 0; i < tgaFiles.size(); ++i) {
        const std::string& filename = tgaFiles[i];
        std::string filepath = joinPath(tgaDir, filename);
        
        TgaTestResult result = readTgaFile(filepath);
        
        if (result.errorCode != 0) {
            stats.recordFail(filename, "decode failed with error " + std::to_string(result.errorCode));
            continue;
        }
        
        if (result.width <= 0 || result.height <= 0) {
            stats.recordFail(filename, "invalid dimensions");
            continue;
        }
        
        // Check CRC if available
        uint32_t expectedCrc;
        if (findTgaExpectedCrc("encoding", filename, expectedCrc)) {
            if (result.crc != expectedCrc) {
                std::ostringstream oss;
                oss << "CRC mismatch: expected " << std::hex << expectedCrc 
                    << " got " << result.crc;
                stats.recordFail(filename, oss.str());
                continue;
            }
        } else {
            missingCrcs.push_back(filename);
        }
        
        stats.recordPass();
    }
    
    if (!missingCrcs.empty()) {
        printf("    NOTE: %zu encoding files have no reference CRC\n", missingCrcs.size());
    }
    
    printf("    %s\n", stats.summary().c_str());
    
    if (!stats.allPassed()) {
        for (size_t i = 0; i < stats.failures.size(); ++i) {
            printf("      FAIL: %s\n", stats.failures[i].c_str());
        }
        ASSERT_TRUE(false);
    }
}

TEST(TgaReader, AllTgaFixtures_MemoryConsistency) {
    // Verify file reading and memory reading produce identical results
    // across both testsuite and encoding directories
    const char* subdirs[] = {"testsuite", "encoding"};
    
    CorpusTestStats stats;
    
    for (int d = 0; d < 2; ++d) {
        std::string tgaDir = joinPath(joinPath(g_TestImagesDir, "tga"), subdirs[d]);
        if (!directoryExists(tgaDir)) continue;
        
        std::vector<std::string> tgaFiles = collectFilesWithExtension(tgaDir, ".tga");
        
        for (size_t i = 0; i < tgaFiles.size(); ++i) {
            const std::string& filename = tgaFiles[i];
            std::string filepath = joinPath(tgaDir, filename);
            
            TgaTestResult fileResult = readTgaFile(filepath);
            if (fileResult.errorCode != 0) {
                stats.recordSkip();
                continue;
            }
            
            std::vector<uint8_t> fileData = readBinaryFile(filepath);
            if (fileData.empty()) {
                stats.recordFail(filename, "failed to read file data");
                continue;
            }
            
            TgaTestResult memResult = readTgaMemory(fileData.data(), static_cast<int>(fileData.size()));
            
            if (memResult.errorCode != fileResult.errorCode) {
                stats.recordFail(filename, "error code mismatch");
                continue;
            }
            
            if (memResult.crc != fileResult.crc) {
                std::ostringstream oss;
                oss << "CRC mismatch: file=" << std::hex << fileResult.crc 
                    << " mem=" << memResult.crc;
                stats.recordFail(filename, oss.str());
                continue;
            }
            
            stats.recordPass();
        }
    }
    
    if (stats.total == 0) SKIP_TEST("No TGA fixtures found");
    
    printf("    %s\n", stats.summary().c_str());
    
    if (!stats.allPassed()) {
        for (size_t i = 0; i < stats.failures.size(); ++i) {
            printf("      FAIL: %s\n", stats.failures[i].c_str());
        }
        ASSERT_TRUE(false);
    }
}

//=============================================================================
// Regression Tests - Malformed/Edge-case TGA Files
// These tests ensure the reader handles problematic files gracefully
//=============================================================================

TEST(TgaReader, RegressionCorpus_MustNotCrash) {
    // Test all regression TGA files - they must not crash
    // Some may decode, some may fail, but none should cause crashes or hangs
    std::string regDir = joinPath(joinPath(g_TestReferenceDir, ".."), "regression/tga");
    if (!directoryExists(regDir)) {
        // Try alternate path
        regDir = joinPath(g_TestImagesDir, "../regression/tga");
    }
    if (!directoryExists(regDir)) SKIP_TEST("TGA regression directory not found");
    
    std::vector<std::string> tgaFiles = collectFilesWithExtension(regDir, ".tga");
    if (tgaFiles.empty()) SKIP_TEST("No TGA regression files found");
    
    CorpusTestStats stats;
    int decodedOk = 0;
    int failedGracefully = 0;
    
    for (size_t i = 0; i < tgaFiles.size(); ++i) {
        const std::string& filename = tgaFiles[i];
        std::string filepath = joinPath(regDir, filename);
        
        // Just try to read - success or graceful failure is OK
        TgaTestResult result = readTgaFile(filepath);
        
        if (result.errorCode == 0) {
            // If it claims success, verify output is reasonable
            if (result.width <= 0 || result.height <= 0 || 
                result.width > 65536 || result.height > 65536) {
                stats.recordFail(filename, "succeeded but returned invalid dimensions");
                continue;
            }
            ++decodedOk;
        } else {
            ++failedGracefully;
        }
        
        stats.recordPass();
    }
    
    printf("    %s (decoded: %d, rejected: %d)\n", 
           stats.summary().c_str(), decodedOk, failedGracefully);
    
    if (!stats.allPassed()) {
        for (size_t i = 0; i < stats.failures.size(); ++i) {
            printf("      FAIL: %s\n", stats.failures[i].c_str());
        }
        ASSERT_TRUE(false);
    }
}

TEST(TgaReader, RegressionCorpus_MemoryMustNotCrash) {
    // Same as above but via memory reading
    std::string regDir = joinPath(joinPath(g_TestReferenceDir, ".."), "regression/tga");
    if (!directoryExists(regDir)) {
        regDir = joinPath(g_TestImagesDir, "../regression/tga");
    }
    if (!directoryExists(regDir)) SKIP_TEST("TGA regression directory not found");
    
    std::vector<std::string> tgaFiles = collectFilesWithExtension(regDir, ".tga");
    if (tgaFiles.empty()) SKIP_TEST("No TGA regression files found");
    
    CorpusTestStats stats;
    
    for (size_t i = 0; i < tgaFiles.size(); ++i) {
        const std::string& filename = tgaFiles[i];
        std::string filepath = joinPath(regDir, filename);
        
        std::vector<uint8_t> fileData = readBinaryFile(filepath);
        if (fileData.empty()) {
            stats.recordSkip();
            continue;
        }
        
        TgaTestResult result = readTgaMemory(fileData.data(), static_cast<int>(fileData.size()));
        
        if (result.errorCode == 0) {
            if (result.width <= 0 || result.height <= 0 || 
                result.width > 65536 || result.height > 65536) {
                stats.recordFail(filename, "succeeded but returned invalid dimensions");
                continue;
            }
        }
        // Either failed or succeeded with valid data - both OK
        stats.recordPass();
    }
    
    printf("    %s\n", stats.summary().c_str());
    
    if (!stats.allPassed()) {
        for (size_t i = 0; i < stats.failures.size(); ++i) {
            printf("      FAIL: %s\n", stats.failures[i].c_str());
        }
        ASSERT_TRUE(false);
    }
}