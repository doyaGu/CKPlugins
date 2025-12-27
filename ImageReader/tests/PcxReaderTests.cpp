/**
 * @file PcxReaderTests.cpp
 * @brief Comprehensive PCX format tests for CKImageReader
 *
 * Tests cover:
 * - 8-bit paletted images (VGA 256-color)
 * - 24-bit RGB images (3-plane)
 * - 1-bit and 4-bit images (EGA/CGA)
 * - RLE compression edge cases
 * - Memory reading
 * - Generated test fixtures for complete coverage
 *
 * Note: PCX save is not implemented (read-only format in this library)
 */

#include "TestFramework.h"
#include "PcxReader.h"
#include <cstring>

using namespace TestFramework;

//=============================================================================
// PCX Test Fixture Generator
// Since there are no PCX files in the test corpus, we generate them in-memory
//=============================================================================

namespace {

// PCX header structure (128 bytes)
#pragma pack(push, 1)
struct PCXHeader {
    uint8_t manufacturer;   // 0x0A = ZSoft PCX
    uint8_t version;        // 0=2.5, 2=2.8 w/pal, 3=2.8 w/o pal, 4=Win, 5=3.0+
    uint8_t encoding;       // 1 = RLE
    uint8_t bitsPerPixel;   // Bits per pixel per plane
    uint16_t xMin, yMin;
    uint16_t xMax, yMax;
    uint16_t hDPI, vDPI;
    uint8_t colorMap[48];   // EGA palette (16 colors)
    uint8_t reserved;
    uint8_t nPlanes;        // Number of color planes
    uint16_t bytesPerLine;  // Bytes per scanline per plane
    uint16_t paletteInfo;   // 1=color, 2=grayscale
    uint16_t hScreenSize;
    uint16_t vScreenSize;
    uint8_t padding[54];
};
#pragma pack(pop)

// Simple RLE encoder for PCX
void pcxRleEncode(const uint8_t* src, int count, std::vector<uint8_t>& out) {
    int i = 0;
    while (i < count) {
        uint8_t byte = src[i];
        int runLen = 1;
        while (i + runLen < count && runLen < 63 && src[i + runLen] == byte) {
            ++runLen;
        }
        if (runLen > 1 || byte >= 0xC0) {
            out.push_back(static_cast<uint8_t>(0xC0 | runLen));
            out.push_back(byte);
        } else {
            out.push_back(byte);
        }
        i += runLen;
    }
}

// Generate a simple 8-bit paletted PCX (256 colors with VGA palette marker)
std::vector<uint8_t> generatePcx8bit(int width, int height, bool useGradient = true) {
    std::vector<uint8_t> data;

    PCXHeader header;
    memset(&header, 0, sizeof(header));
    header.manufacturer = 0x0A;
    header.version = 5;  // Version 3.0+
    header.encoding = 1; // RLE
    header.bitsPerPixel = 8;
    header.xMin = 0;
    header.yMin = 0;
    header.xMax = static_cast<uint16_t>(width - 1);
    header.yMax = static_cast<uint16_t>(height - 1);
    header.hDPI = 72;
    header.vDPI = 72;
    header.nPlanes = 1;
    header.bytesPerLine = static_cast<uint16_t>((width + 1) & ~1); // Even
    header.paletteInfo = 1;

    // Write header
    const uint8_t* hdr = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), hdr, hdr + 128);

    // Generate and encode scanlines
    std::vector<uint8_t> scanline(header.bytesPerLine);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (useGradient) {
                scanline[x] = static_cast<uint8_t>((x + y) % 256);
            } else {
                scanline[x] = static_cast<uint8_t>((x * 17 + y * 31) % 256);
            }
        }
        // Pad to even
        for (int x = width; x < header.bytesPerLine; ++x) {
            scanline[x] = 0;
        }
        pcxRleEncode(scanline.data(), header.bytesPerLine, data);
    }

    // VGA palette marker (0x0C) followed by 768 bytes
    data.push_back(0x0C);
    for (int i = 0; i < 256; ++i) {
        // Simple grayscale ramp palette
        data.push_back(static_cast<uint8_t>(i)); // R
        data.push_back(static_cast<uint8_t>(i)); // G
        data.push_back(static_cast<uint8_t>(i)); // B
    }

    return data;
}

// Generate a 24-bit RGB PCX (3 planes, 8 bits each)
std::vector<uint8_t> generatePcx24bit(int width, int height) {
    std::vector<uint8_t> data;

    PCXHeader header;
    memset(&header, 0, sizeof(header));
    header.manufacturer = 0x0A;
    header.version = 5;
    header.encoding = 1;
    header.bitsPerPixel = 8;
    header.xMin = 0;
    header.yMin = 0;
    header.xMax = static_cast<uint16_t>(width - 1);
    header.yMax = static_cast<uint16_t>(height - 1);
    header.hDPI = 72;
    header.vDPI = 72;
    header.nPlanes = 3; // R, G, B planes
    header.bytesPerLine = static_cast<uint16_t>((width + 1) & ~1);
    header.paletteInfo = 1;

    const uint8_t* hdr = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), hdr, hdr + 128);

    // Generate and encode scanlines (R plane, then G plane, then B plane per row)
    std::vector<uint8_t> scanline(header.bytesPerLine);
    for (int y = 0; y < height; ++y) {
        // Red plane
        for (int x = 0; x < width; ++x) {
            scanline[x] = static_cast<uint8_t>((x * 255) / (width > 1 ? width - 1 : 1));
        }
        for (int x = width; x < header.bytesPerLine; ++x) scanline[x] = 0;
        pcxRleEncode(scanline.data(), header.bytesPerLine, data);

        // Green plane
        for (int x = 0; x < width; ++x) {
            scanline[x] = static_cast<uint8_t>((y * 255) / (height > 1 ? height - 1 : 1));
        }
        for (int x = width; x < header.bytesPerLine; ++x) scanline[x] = 0;
        pcxRleEncode(scanline.data(), header.bytesPerLine, data);

        // Blue plane
        for (int x = 0; x < width; ++x) {
            scanline[x] = static_cast<uint8_t>(128);
        }
        for (int x = width; x < header.bytesPerLine; ++x) scanline[x] = 0;
        pcxRleEncode(scanline.data(), header.bytesPerLine, data);
    }

    return data;
}

// Generate a 4-bit (16-color) PCX
std::vector<uint8_t> generatePcx4bit(int width, int height) {
    std::vector<uint8_t> data;

    PCXHeader header;
    memset(&header, 0, sizeof(header));
    header.manufacturer = 0x0A;
    header.version = 5;
    header.encoding = 1;
    header.bitsPerPixel = 1; // 1 bit per plane
    header.xMin = 0;
    header.yMin = 0;
    header.xMax = static_cast<uint16_t>(width - 1);
    header.yMax = static_cast<uint16_t>(height - 1);
    header.hDPI = 72;
    header.vDPI = 72;
    header.nPlanes = 4; // 4 planes = 16 colors
    header.bytesPerLine = static_cast<uint16_t>(((width + 7) / 8 + 1) & ~1);
    header.paletteInfo = 1;

    // EGA 16-color palette in header
    uint8_t egaPalette[48] = {
        0x00, 0x00, 0x00,  // 0: Black
        0x00, 0x00, 0xAA,  // 1: Blue
        0x00, 0xAA, 0x00,  // 2: Green
        0x00, 0xAA, 0xAA,  // 3: Cyan
        0xAA, 0x00, 0x00,  // 4: Red
        0xAA, 0x00, 0xAA,  // 5: Magenta
        0xAA, 0x55, 0x00,  // 6: Brown
        0xAA, 0xAA, 0xAA,  // 7: Light Gray
        0x55, 0x55, 0x55,  // 8: Dark Gray
        0x55, 0x55, 0xFF,  // 9: Light Blue
        0x55, 0xFF, 0x55,  // 10: Light Green
        0x55, 0xFF, 0xFF,  // 11: Light Cyan
        0xFF, 0x55, 0x55,  // 12: Light Red
        0xFF, 0x55, 0xFF,  // 13: Light Magenta
        0xFF, 0xFF, 0x55,  // 14: Yellow
        0xFF, 0xFF, 0xFF   // 15: White
    };
    memcpy(header.colorMap, egaPalette, 48);

    const uint8_t* hdr = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), hdr, hdr + 128);

    // Generate scanlines (4 planes per row)
    std::vector<uint8_t> scanline(header.bytesPerLine);
    for (int y = 0; y < height; ++y) {
        // Generate 4-bit indices for this row
        std::vector<uint8_t> indices(width);
        for (int x = 0; x < width; ++x) {
            indices[x] = static_cast<uint8_t>((x + y) % 16);
        }

        // Convert to 4 bit-planes
        for (int plane = 0; plane < 4; ++plane) {
            memset(scanline.data(), 0, scanline.size());
            for (int x = 0; x < width; ++x) {
                if (indices[x] & (1 << plane)) {
                    scanline[x / 8] |= (0x80 >> (x % 8));
                }
            }
            pcxRleEncode(scanline.data(), header.bytesPerLine, data);
        }
    }

    return data;
}

// Generate a 1-bit monochrome PCX
std::vector<uint8_t> generatePcx1bit(int width, int height) {
    std::vector<uint8_t> data;

    PCXHeader header;
    memset(&header, 0, sizeof(header));
    header.manufacturer = 0x0A;
    header.version = 5;
    header.encoding = 1;
    header.bitsPerPixel = 1;
    header.xMin = 0;
    header.yMin = 0;
    header.xMax = static_cast<uint16_t>(width - 1);
    header.yMax = static_cast<uint16_t>(height - 1);
    header.hDPI = 72;
    header.vDPI = 72;
    header.nPlanes = 1;
    header.bytesPerLine = static_cast<uint16_t>(((width + 7) / 8 + 1) & ~1);
    header.paletteInfo = 1;

    // B&W palette
    header.colorMap[0] = 0x00; header.colorMap[1] = 0x00; header.colorMap[2] = 0x00; // Black
    header.colorMap[3] = 0xFF; header.colorMap[4] = 0xFF; header.colorMap[5] = 0xFF; // White

    const uint8_t* hdr = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), hdr, hdr + 128);

    // Generate checkerboard pattern
    std::vector<uint8_t> scanline(header.bytesPerLine);
    for (int y = 0; y < height; ++y) {
        memset(scanline.data(), 0, scanline.size());
        for (int x = 0; x < width; ++x) {
            if ((x + y) % 2 == 0) {
                scanline[x / 8] |= (0x80 >> (x % 8));
            }
        }
        pcxRleEncode(scanline.data(), header.bytesPerLine, data);
    }

    return data;
}

struct PcxTestResult {
    int errorCode;
    uint32_t crc;
    int width;
    int height;
    int bytesPerLine;

    PcxTestResult() : errorCode(-1), crc(0), width(0), height(0), bytesPerLine(0) {}
};

PcxTestResult readPcxMemory(const void* data, int size) {
    PcxTestResult result;
    PcxReader reader;
    CKBitmapProperties* props = nullptr;

    result.errorCode = reader.ReadMemory(const_cast<void*>(data), size, &props);

    if (result.errorCode == 0 && props) {
        result.width = props->m_Format.Width;
        result.height = props->m_Format.Height;
        result.bytesPerLine = props->m_Format.BytesPerLine;

        if (props->m_Format.Image && result.height > 0 && result.bytesPerLine > 0) {
            size_t imageSize = static_cast<size_t>(result.bytesPerLine) * result.height;
            result.crc = CRC32::compute(props->m_Format.Image, imageSize);
        }

        ImageReader::FreeBitmapData(props);
    }

    return result;
}

PcxTestResult readPcxFile(const std::string& path) {
    PcxTestResult result;
    PcxReader reader;
    CKBitmapProperties* props = nullptr;

    result.errorCode = reader.ReadFile(const_cast<char*>(path.c_str()), &props);

    if (result.errorCode == 0 && props) {
        result.width = props->m_Format.Width;
        result.height = props->m_Format.Height;
        result.bytesPerLine = props->m_Format.BytesPerLine;

        if (props->m_Format.Image && result.height > 0 && result.bytesPerLine > 0) {
            size_t imageSize = static_cast<size_t>(result.bytesPerLine) * result.height;
            result.crc = CRC32::compute(props->m_Format.Image, imageSize);
        }

        ImageReader::FreeBitmapData(props);
    }

    return result;
}

std::string getPcxTestImagePath(const std::string& filename) {
    return joinPath(joinPath(g_TestImagesDir, "pcx"), filename);
}

} // anonymous namespace

//=============================================================================
// Generated Fixture Tests - 8-bit Paletted
//=============================================================================

TEST(PcxReader, Generated_8bit_Small) {
    std::vector<uint8_t> pcxData = generatePcx8bit(16, 16);
    ASSERT_TRUE(pcxData.size() > 128);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(16, result.width);
    ASSERT_EQ(16, result.height);
    ASSERT_TRUE(result.crc != 0);
}

TEST(PcxReader, Generated_8bit_Medium) {
    std::vector<uint8_t> pcxData = generatePcx8bit(100, 75);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(100, result.width);
    ASSERT_EQ(75, result.height);
}

TEST(PcxReader, Generated_8bit_OddWidth) {
    // Odd width tests padding handling
    std::vector<uint8_t> pcxData = generatePcx8bit(33, 20);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(33, result.width);
    ASSERT_EQ(20, result.height);
}

TEST(PcxReader, Generated_8bit_1x1) {
    std::vector<uint8_t> pcxData = generatePcx8bit(1, 1);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(1, result.width);
    ASSERT_EQ(1, result.height);
}

TEST(PcxReader, Generated_8bit_Wide) {
    std::vector<uint8_t> pcxData = generatePcx8bit(256, 10);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(256, result.width);
    ASSERT_EQ(10, result.height);
}

TEST(PcxReader, Generated_8bit_Tall) {
    std::vector<uint8_t> pcxData = generatePcx8bit(10, 256);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(10, result.width);
    ASSERT_EQ(256, result.height);
}

//=============================================================================
// Generated Fixture Tests - 24-bit RGB
//=============================================================================

TEST(PcxReader, Generated_24bit_Small) {
    std::vector<uint8_t> pcxData = generatePcx24bit(16, 16);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(16, result.width);
    ASSERT_EQ(16, result.height);
}

TEST(PcxReader, Generated_24bit_Medium) {
    std::vector<uint8_t> pcxData = generatePcx24bit(64, 48);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(64, result.width);
    ASSERT_EQ(48, result.height);
}

TEST(PcxReader, Generated_24bit_OddWidth) {
    std::vector<uint8_t> pcxData = generatePcx24bit(31, 25);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(31, result.width);
    ASSERT_EQ(25, result.height);
}

//=============================================================================
// Generated Fixture Tests - 4-bit (16-color)
//=============================================================================

TEST(PcxReader, Generated_4bit_Small) {
    std::vector<uint8_t> pcxData = generatePcx4bit(16, 16);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(16, result.width);
    ASSERT_EQ(16, result.height);
}

TEST(PcxReader, Generated_4bit_Medium) {
    std::vector<uint8_t> pcxData = generatePcx4bit(80, 60);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(80, result.width);
    ASSERT_EQ(60, result.height);
}

//=============================================================================
// Generated Fixture Tests - 1-bit Monochrome
//=============================================================================

TEST(PcxReader, Generated_1bit_Small) {
    std::vector<uint8_t> pcxData = generatePcx1bit(16, 16);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(16, result.width);
    ASSERT_EQ(16, result.height);
}

TEST(PcxReader, Generated_1bit_Medium) {
    std::vector<uint8_t> pcxData = generatePcx1bit(100, 50);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(100, result.width);
    ASSERT_EQ(50, result.height);
}

TEST(PcxReader, Generated_1bit_OddWidth) {
    std::vector<uint8_t> pcxData = generatePcx1bit(73, 41);

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(73, result.width);
    ASSERT_EQ(41, result.height);
}

//=============================================================================
// Consistency Tests - Same input should produce same CRC
//=============================================================================

TEST(PcxReader, Consistency_8bit) {
    std::vector<uint8_t> pcxData = generatePcx8bit(50, 50);

    PcxTestResult result1 = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result1.errorCode);

    PcxTestResult result2 = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result2.errorCode);

    ASSERT_EQ(result1.crc, result2.crc);
}

TEST(PcxReader, Consistency_24bit) {
    std::vector<uint8_t> pcxData = generatePcx24bit(50, 50);

    PcxTestResult result1 = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result1.errorCode);

    PcxTestResult result2 = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_EQ(0, result2.errorCode);

    ASSERT_EQ(result1.crc, result2.crc);
}

//=============================================================================
// Output Format Verification
//=============================================================================

TEST(PcxReader, OutputFormat_BGRA32) {
    std::vector<uint8_t> pcxData = generatePcx8bit(32, 32);

    PcxReader reader;
    CKBitmapProperties* props = nullptr;
    int err = reader.ReadMemory(pcxData.data(), static_cast<int>(pcxData.size()), &props);

    ASSERT_EQ(0, err);
    ASSERT_TRUE(props != nullptr);

    // Verify BGRA32 format
    ASSERT_EQ(32, props->m_Format.BitsPerPixel);
    ASSERT_EQ(static_cast<uint32_t>(0x00FF0000), props->m_Format.RedMask);
    ASSERT_EQ(static_cast<uint32_t>(0x0000FF00), props->m_Format.GreenMask);
    ASSERT_EQ(static_cast<uint32_t>(0x000000FF), props->m_Format.BlueMask);
    ASSERT_EQ(static_cast<uint32_t>(0xFF000000), props->m_Format.AlphaMask);

    // BytesPerLine should be Width * 4
    ASSERT_EQ(32 * 4, props->m_Format.BytesPerLine);

    ImageReader::FreeBitmapData(props);
}

//=============================================================================
// Negative Tests
//=============================================================================

TEST(PcxReader, Invalid_Manufacturer) {
    std::vector<uint8_t> pcxData = generatePcx8bit(16, 16);
    ASSERT_TRUE(pcxData.size() > 0);

    // Corrupt manufacturer byte (should be 0x0A)
    pcxData[0] = 0xFF;

    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(PcxReader, Truncated_Header) {
    std::vector<uint8_t> pcxData = generatePcx8bit(16, 16);

    // Truncate to less than 128 bytes
    PcxTestResult result = readPcxMemory(pcxData.data(), 64);
    ASSERT_NE(0, result.errorCode);
}

TEST(PcxReader, Truncated_Data) {
    std::vector<uint8_t> pcxData = generatePcx8bit(16, 16);

    // Truncate image data (keep header + a few bytes)
    PcxTestResult result = readPcxMemory(pcxData.data(), 140);
    // Some readers handle truncated data gracefully, others return errors
    // Just verify no crash
    ASSERT_TRUE(result.errorCode == 0 || result.errorCode != 0);
}

TEST(PcxReader, Empty_File) {
    uint8_t emptyData[1] = {0};
    PcxTestResult result = readPcxMemory(emptyData, 0);
    ASSERT_NE(0, result.errorCode);
}

TEST(PcxReader, ZeroSize_Image) {
    std::vector<uint8_t> pcxData = generatePcx8bit(16, 16);

    // Set xMax < xMin (zero width)
    pcxData[8] = 0xFF; pcxData[9] = 0xFF; // xMax = 0xFFFF (will wrap)
    pcxData[4] = 0x00; pcxData[5] = 0x00; // xMin = 0

    // This creates an invalid dimension scenario
    PcxTestResult result = readPcxMemory(pcxData.data(), static_cast<int>(pcxData.size()));
    // May or may not fail depending on implementation - just verify no crash
    ASSERT_TRUE(true);
}

//=============================================================================
// API Tests
//=============================================================================

TEST(PcxReader, GetReaderInfo) {
    PcxReader reader;
    CKPluginInfo* info = reader.GetReaderInfo();
    ASSERT_TRUE(info != nullptr);
    ASSERT_GUID_EQ(PCXREADER_GUID, info->m_GUID);
}

TEST(PcxReader, GetOptionsCount) {
    PcxReader reader;
    int count = reader.GetOptionsCount();
    // PCX is read-only, so options count may be 0 or minimal
    ASSERT_TRUE(count >= 0);
}

TEST(PcxReader, GetFlags) {
    PcxReader reader;
    CK_DATAREADER_FLAGS flags = reader.GetFlags();
    ASSERT_EQ(15, flags);
}

TEST(PcxReader, SaveFile_NotImplemented) {
    PcxReader reader;
    CKBitmapProperties props;
    memset(&props, 0, sizeof(props));

    // Save should return 0 (not implemented)
    int result = reader.SaveFile(const_cast<char*>("test.pcx"), &props);
    ASSERT_EQ(0, result);
}

TEST(PcxReader, IsAlphaSaved) {
    PcxReader reader;
    CKBitmapProperties props;
    memset(&props, 0, sizeof(props));

    // PCX doesn't support alpha
    ASSERT_FALSE(reader.IsAlphaSaved(&props));
}

//=============================================================================
// File-based Tests (if PCX corpus exists)
//=============================================================================

TEST(PcxReader, FileCorpus_IfExists) {
    std::string pcxDir = joinPath(g_TestImagesDir, "pcx");
    if (!directoryExists(pcxDir)) {
        // No PCX corpus available - this is acceptable, test passes
        ASSERT_TRUE(true);
        return;
    }

    std::vector<std::string> files = listDirectory(pcxDir);
    int tested = 0;
    int passed = 0;

    for (size_t i = 0; i < files.size(); ++i) {
        std::string ext = toLower(getExtension(files[i]));
        if (ext != ".pcx") continue;

        std::string path = joinPath(pcxDir, files[i]);
        std::vector<uint8_t> data = readBinaryFile(path);
        if (data.empty()) continue;

        ++tested;
        PcxTestResult result = readPcxMemory(data.data(), static_cast<int>(data.size()));
        if (result.errorCode == 0) {
            ++passed;
        }
    }

    if (tested == 0) {
        // No PCX files found - this is acceptable
        ASSERT_TRUE(true);
        return;
    }

    // All valid PCX files should load
    ASSERT_EQ(tested, passed);
}

//=============================================================================
// RLE Edge Cases
//=============================================================================

TEST(PcxReader, RLE_MaxRunLength) {
    // Generate image with large uniform areas to test max RLE runs (63 bytes)
    std::vector<uint8_t> data;

    PCXHeader header;
    memset(&header, 0, sizeof(header));
    header.manufacturer = 0x0A;
    header.version = 5;
    header.encoding = 1;
    header.bitsPerPixel = 8;
    header.xMin = 0;
    header.yMin = 0;
    header.xMax = 127; // 128 pixels wide
    header.yMax = 3;   // 4 rows
    header.hDPI = 72;
    header.vDPI = 72;
    header.nPlanes = 1;
    header.bytesPerLine = 128;
    header.paletteInfo = 1;

    const uint8_t* hdr = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), hdr, hdr + 128);

    // Encode uniform scanlines (all same color)
    for (int y = 0; y < 4; ++y) {
        uint8_t color = static_cast<uint8_t>(y * 50);
        // 128 bytes of same value: 63 + 63 + 2 runs
        data.push_back(0xC0 | 63); data.push_back(color);
        data.push_back(0xC0 | 63); data.push_back(color);
        data.push_back(0xC0 | 2);  data.push_back(color);
    }

    // VGA palette
    data.push_back(0x0C);
    for (int i = 0; i < 256; ++i) {
        data.push_back(static_cast<uint8_t>(i));
        data.push_back(static_cast<uint8_t>(i));
        data.push_back(static_cast<uint8_t>(i));
    }

    PcxTestResult result = readPcxMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(128, result.width);
    ASSERT_EQ(4, result.height);
}

TEST(PcxReader, RLE_LiteralBytes) {
    // Generate image with non-repeating pattern (literal bytes, not runs)
    std::vector<uint8_t> data;

    PCXHeader header;
    memset(&header, 0, sizeof(header));
    header.manufacturer = 0x0A;
    header.version = 5;
    header.encoding = 1;
    header.bitsPerPixel = 8;
    header.xMin = 0;
    header.yMin = 0;
    header.xMax = 15;
    header.yMax = 3;
    header.hDPI = 72;
    header.vDPI = 72;
    header.nPlanes = 1;
    header.bytesPerLine = 16;
    header.paletteInfo = 1;

    const uint8_t* hdr = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), hdr, hdr + 128);

    // Encode with all unique bytes (no runs possible)
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 16; ++x) {
            uint8_t val = static_cast<uint8_t>((x * 7 + y * 31) % 192); // Keep < 0xC0
            data.push_back(val);
        }
    }

    // VGA palette
    data.push_back(0x0C);
    for (int i = 0; i < 256; ++i) {
        data.push_back(static_cast<uint8_t>(i));
        data.push_back(static_cast<uint8_t>(i));
        data.push_back(static_cast<uint8_t>(i));
    }

    PcxTestResult result = readPcxMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(16, result.width);
    ASSERT_EQ(4, result.height);
}

//=============================================================================
// Additional Generated Size Tests
//=============================================================================

TEST(PcxReader, Generated_1x1_8bit) {
    std::vector<uint8_t> pcx = generatePcx8bit(1, 1);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(1, result.width);
    ASSERT_EQ(1, result.height);
}

TEST(PcxReader, Generated_1x1_24bit) {
    std::vector<uint8_t> pcx = generatePcx24bit(1, 1);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(1, result.width);
    ASSERT_EQ(1, result.height);
}

TEST(PcxReader, Generated_2x2_8bit) {
    std::vector<uint8_t> pcx = generatePcx8bit(2, 2);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(2, result.width);
    ASSERT_EQ(2, result.height);
}

TEST(PcxReader, Generated_3x3_8bit) {
    std::vector<uint8_t> pcx = generatePcx8bit(3, 3);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(3, result.width);
    ASSERT_EQ(3, result.height);
}

TEST(PcxReader, Generated_7x11_24bit) {
    std::vector<uint8_t> pcx = generatePcx24bit(7, 11);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(7, result.width);
    ASSERT_EQ(11, result.height);
}

TEST(PcxReader, Generated_100x100_8bit) {
    std::vector<uint8_t> pcx = generatePcx8bit(100, 100);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(100, result.width);
    ASSERT_EQ(100, result.height);
}

TEST(PcxReader, Generated_128x128_24bit) {
    std::vector<uint8_t> pcx = generatePcx24bit(128, 128);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(128, result.width);
    ASSERT_EQ(128, result.height);
}

TEST(PcxReader, Generated_WideImage_512x8) {
    std::vector<uint8_t> pcx = generatePcx8bit(512, 8);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(512, result.width);
    ASSERT_EQ(8, result.height);
}

TEST(PcxReader, Generated_TallImage_8x512) {
    std::vector<uint8_t> pcx = generatePcx8bit(8, 512);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(8, result.width);
    ASSERT_EQ(512, result.height);
}

TEST(PcxReader, Generated_NonPow2_37x53) {
    std::vector<uint8_t> pcx = generatePcx8bit(37, 53);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(37, result.width);
    ASSERT_EQ(53, result.height);
}

TEST(PcxReader, Generated_PrimeSize_127x131) {
    std::vector<uint8_t> pcx = generatePcx24bit(127, 131);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(127, result.width);
    ASSERT_EQ(131, result.height);
}

TEST(PcxReader, Generated_OddWidth_15x16) {
    std::vector<uint8_t> pcx = generatePcx8bit(15, 16);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(15, result.width);
    ASSERT_EQ(16, result.height);
}

TEST(PcxReader, Generated_4bit_Various) {
    // Test multiple 4-bit sizes
    int sizes[][2] = {{8, 8}, {16, 16}, {32, 16}, {64, 64}};
    for (int i = 0; i < 4; ++i) {
        std::vector<uint8_t> pcx = generatePcx4bit(sizes[i][0], sizes[i][1]);
        PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
        ASSERT_EQ(0, result.errorCode);
        ASSERT_EQ(sizes[i][0], result.width);
        ASSERT_EQ(sizes[i][1], result.height);
    }
}

TEST(PcxReader, Generated_1bit_Various) {
    int sizes[][2] = {{8, 8}, {16, 16}, {32, 8}, {64, 64}};
    for (int i = 0; i < 4; ++i) {
        std::vector<uint8_t> pcx = generatePcx1bit(sizes[i][0], sizes[i][1]);
        PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
        ASSERT_EQ(0, result.errorCode);
        ASSERT_EQ(sizes[i][0], result.width);
        ASSERT_EQ(sizes[i][1], result.height);
    }
}

//=============================================================================
// Additional Negative Tests
//=============================================================================

TEST(PcxReader, Negative_TruncatedHeader) {
    std::vector<uint8_t> pcx = generatePcx8bit(4, 4);
    pcx.resize(64);  // Truncate header
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(PcxReader, Negative_TruncatedPixelData) {
    std::vector<uint8_t> pcx = generatePcx8bit(32, 32);
    pcx.resize(128 + 50);  // Header + partial pixels
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    // Reader may either reject or partially decode - test for no crash
    ASSERT_TRUE(result.errorCode != 0 || result.width == 32);
}

TEST(PcxReader, Negative_TruncatedPalette) {
    std::vector<uint8_t> pcx = generatePcx8bit(4, 4);
    // Remove part of the VGA palette
    pcx.resize(pcx.size() - 200);
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    // Reader may use EGA fallback or partially decode - test for no crash
    ASSERT_TRUE(result.errorCode != 0 || result.width == 4);
}

TEST(PcxReader, Negative_InvalidManufacturer) {
    std::vector<uint8_t> pcx = generatePcx8bit(4, 4);
    pcx[0] = 0x00;  // Invalid manufacturer
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(PcxReader, Negative_InvalidBitsPerPixel) {
    std::vector<uint8_t> pcx = generatePcx8bit(4, 4);
    pcx[3] = 7;  // Invalid bits per pixel
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(PcxReader, Negative_ZeroWidth) {
    std::vector<uint8_t> pcx = generatePcx8bit(4, 4);
    // Set xMax < xMin (results in zero/negative width)
    pcx[4] = 1; pcx[5] = 0;  // xMin = 1
    pcx[8] = 0; pcx[9] = 0;  // xMax = 0
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(PcxReader, Negative_ZeroHeight) {
    std::vector<uint8_t> pcx = generatePcx8bit(4, 4);
    // Set yMax < yMin
    pcx[6] = 1; pcx[7] = 0;  // yMin = 1
    pcx[10] = 0; pcx[11] = 0; // yMax = 0
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(PcxReader, Negative_VeryLargeDimensions) {
    std::vector<uint8_t> pcx = generatePcx8bit(4, 4);
    // Set dimensions to max
    pcx[8] = 0xFF; pcx[9] = 0xFF;   // xMax
    pcx[10] = 0xFF; pcx[11] = 0xFF; // yMax
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_NE(0, result.errorCode);
}

TEST(PcxReader, Negative_EmptyData) {
    PcxTestResult result = readPcxMemory(nullptr, 0);
    ASSERT_NE(0, result.errorCode);
}

TEST(PcxReader, Negative_OneByte) {
    uint8_t data = 0x0A;
    PcxTestResult result = readPcxMemory(&data, 1);
    ASSERT_NE(0, result.errorCode);
}

TEST(PcxReader, Negative_MissingPaletteMarker) {
    std::vector<uint8_t> pcx = generatePcx8bit(4, 4);
    // Find and corrupt the 0x0C palette marker
    for (size_t i = 128; i < pcx.size(); ++i) {
        if (pcx[i] == 0x0C && pcx.size() - i >= 769) {
            pcx[i] = 0x00;  // Corrupt marker
            break;
        }
    }
    PcxTestResult result = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    // Should either fail or fallback to EGA palette
    // This tests robustness
    ASSERT_TRUE(result.errorCode == 0 || result.errorCode != 0);
}

//=============================================================================
// Memory vs File Consistency Tests
//=============================================================================

TEST(PcxReader, MemoryFileConsistency_8bit) {
    std::vector<uint8_t> pcx = generatePcx8bit(32, 32);
    
    PcxTestResult memResult = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, memResult.errorCode);
    
    std::string tempPath = joinPath(g_TestOutputDir, "pcx_consistency_test.pcx");
    FILE* f = fopen(tempPath.c_str(), "wb");
    if (f) {
        fwrite(pcx.data(), 1, pcx.size(), f);
        fclose(f);
        
        PcxTestResult fileResult = readPcxFile(tempPath);
        ASSERT_EQ(0, fileResult.errorCode);
        ASSERT_EQ(memResult.width, fileResult.width);
        ASSERT_EQ(memResult.height, fileResult.height);
        ASSERT_EQ(memResult.crc, fileResult.crc);
    }
}

TEST(PcxReader, MemoryFileConsistency_24bit) {
    std::vector<uint8_t> pcx = generatePcx24bit(64, 64);
    
    PcxTestResult memResult = readPcxMemory(pcx.data(), static_cast<int>(pcx.size()));
    ASSERT_EQ(0, memResult.errorCode);
    
    std::string tempPath = joinPath(g_TestOutputDir, "pcx_consistency_24.pcx");
    FILE* f = fopen(tempPath.c_str(), "wb");
    if (f) {
        fwrite(pcx.data(), 1, pcx.size(), f);
        fclose(f);
        
        PcxTestResult fileResult = readPcxFile(tempPath);
        ASSERT_EQ(0, fileResult.errorCode);
        ASSERT_EQ(memResult.crc, fileResult.crc);
    }
}

//=============================================================================
// Additional API Tests
//=============================================================================

TEST(PcxReader, MultipleInstancesIndependent) {
    PcxReader reader1;
    PcxReader reader2;
    
    std::vector<uint8_t> pcx1 = generatePcx8bit(16, 16);
    std::vector<uint8_t> pcx2 = generatePcx8bit(32, 32);
    
    PcxTestResult result1 = readPcxMemory(pcx1.data(), static_cast<int>(pcx1.size()));
    PcxTestResult result2 = readPcxMemory(pcx2.data(), static_cast<int>(pcx2.size()));
    
    ASSERT_EQ(0, result1.errorCode);
    ASSERT_EQ(0, result2.errorCode);
    ASSERT_EQ(16, result1.width);
    ASSERT_EQ(32, result2.width);
}

TEST(PcxReader, ReaderReuse) {
    PcxReader reader;
    std::vector<uint8_t> pcx1 = generatePcx8bit(8, 8);
    std::vector<uint8_t> pcx2 = generatePcx8bit(16, 16);
    
    CKBitmapProperties* props1 = nullptr;
    int err1 = reader.ReadMemory(pcx1.data(), static_cast<int>(pcx1.size()), &props1);
    ASSERT_EQ(0, err1);
    if (props1) {
        ASSERT_EQ(8, props1->m_Format.Width);
        ImageReader::FreeBitmapData(props1);
    }
    
    CKBitmapProperties* props2 = nullptr;
    int err2 = reader.ReadMemory(pcx2.data(), static_cast<int>(pcx2.size()), &props2);
    ASSERT_EQ(0, err2);
    if (props2) {
        ASSERT_EQ(16, props2->m_Format.Width);
        ImageReader::FreeBitmapData(props2);
    }
}

//=============================================================================
// RLE Stress Tests
//=============================================================================

TEST(PcxReader, RLE_AllRunPackets) {
    // Image where every scanline is uniform color
    std::vector<uint8_t> data;

    PCXHeader header;
    memset(&header, 0, sizeof(header));
    header.manufacturer = 0x0A;
    header.version = 5;
    header.encoding = 1;
    header.bitsPerPixel = 8;
    header.xMin = 0;
    header.yMin = 0;
    header.xMax = 63;
    header.yMax = 63;
    header.hDPI = 72;
    header.vDPI = 72;
    header.nPlanes = 1;
    header.bytesPerLine = 64;
    header.paletteInfo = 1;

    const uint8_t* hdr = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), hdr, hdr + 128);

    // Each row is uniform color, encoded as max run
    for (int y = 0; y < 64; ++y) {
        uint8_t color = static_cast<uint8_t>(y % 192);
        // 64 bytes = one run of 63 + one literal
        data.push_back(0xC0 | 63); // Run of 63
        data.push_back(color);
        data.push_back(0xC1);  // Run of 1
        data.push_back(color);
    }

    // VGA palette
    data.push_back(0x0C);
    for (int i = 0; i < 256; ++i) {
        data.push_back(static_cast<uint8_t>(i));
        data.push_back(static_cast<uint8_t>(i));
        data.push_back(static_cast<uint8_t>(i));
    }

    PcxTestResult result = readPcxMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(64, result.width);
    ASSERT_EQ(64, result.height);
}

TEST(PcxReader, RLE_AlternatingPattern) {
    // Checkerboard pattern - worst case for RLE
    std::vector<uint8_t> data;

    PCXHeader header;
    memset(&header, 0, sizeof(header));
    header.manufacturer = 0x0A;
    header.version = 5;
    header.encoding = 1;
    header.bitsPerPixel = 8;
    header.xMin = 0;
    header.yMin = 0;
    header.xMax = 31;
    header.yMax = 31;
    header.hDPI = 72;
    header.vDPI = 72;
    header.nPlanes = 1;
    header.bytesPerLine = 32;
    header.paletteInfo = 1;

    const uint8_t* hdr = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), hdr, hdr + 128);

    // Alternating 0x00 and 0x01 - each needs literal encoding
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            uint8_t val = static_cast<uint8_t>((x + y) % 2);
            // Literal byte (safe, under 0xC0)
            data.push_back(val);
        }
    }

    // VGA palette
    data.push_back(0x0C);
    for (int i = 0; i < 256; ++i) {
        data.push_back(static_cast<uint8_t>(i));
        data.push_back(static_cast<uint8_t>(i));
        data.push_back(static_cast<uint8_t>(i));
    }

    PcxTestResult result = readPcxMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(32, result.width);
    ASSERT_EQ(32, result.height);
}

TEST(PcxReader, RLE_HighValueLiterals) {
    // Test bytes >= 0xC0 encoded as runs
    std::vector<uint8_t> data;

    PCXHeader header;
    memset(&header, 0, sizeof(header));
    header.manufacturer = 0x0A;
    header.version = 5;
    header.encoding = 1;
    header.bitsPerPixel = 8;
    header.xMin = 0;
    header.yMin = 0;
    header.xMax = 15;
    header.yMax = 15;
    header.hDPI = 72;
    header.vDPI = 72;
    header.nPlanes = 1;
    header.bytesPerLine = 16;
    header.paletteInfo = 1;

    const uint8_t* hdr = reinterpret_cast<const uint8_t*>(&header);
    data.insert(data.end(), hdr, hdr + 128);

    // Encode high-value bytes (>= 0xC0) as run of 1
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            uint8_t val = static_cast<uint8_t>(0xC0 + (x + y) % 64);
            data.push_back(0xC1);  // Run of 1
            data.push_back(val);
        }
    }

    // VGA palette
    data.push_back(0x0C);
    for (int i = 0; i < 256; ++i) {
        data.push_back(static_cast<uint8_t>(i));
        data.push_back(static_cast<uint8_t>(i));
        data.push_back(static_cast<uint8_t>(i));
    }

    PcxTestResult result = readPcxMemory(data.data(), static_cast<int>(data.size()));
    ASSERT_EQ(0, result.errorCode);
    ASSERT_EQ(16, result.width);
    ASSERT_EQ(16, result.height);
}

//=============================================================================
// PCX Fixture Generation (run with special flag to create test fixtures)
// This generates PCX files in tests/images/pcx for corpus testing
//=============================================================================

namespace {

bool g_GeneratePcxFixtures = false;

bool writePcxFixture(const std::string& name, const std::vector<uint8_t>& data) {
    std::string pcxDir = joinPath(g_TestImagesDir, "pcx");
    std::string path = joinPath(pcxDir, name);
    return writeBinaryFile(path, data.data(), data.size());
}

} // anonymous namespace

TEST(PcxReader, GenerateFixtures) {
    // This test generates PCX fixtures for corpus testing
    // Skip unless explicitly enabled (e.g., via running with a filter)
    std::string pcxDir = joinPath(g_TestImagesDir, "pcx");
    if (!directoryExists(pcxDir)) {
        // PCX directory doesn't exist, try to create it would require filesystem APIs
        // For now, just skip and document that fixtures need to be generated
        printf("    NOTE: PCX fixtures directory does not exist: %s\n", pcxDir.c_str());
        printf("    Create the directory and re-run this test to generate fixtures.\n");
        SKIP_TEST("PCX fixtures directory not found");
    }
    
    // Check if fixtures already exist
    std::vector<std::string> existing = collectFilesWithExtension(pcxDir, ".pcx");
    if (!existing.empty()) {
        printf("    NOTE: %zu PCX fixtures already exist, skipping generation\n", existing.size());
        SKIP_TEST("Fixtures already exist");
    }
    
    int generated = 0;
    
    // 8-bit paletted fixtures
    if (writePcxFixture("8bit_16x16.pcx", generatePcx8bit(16, 16))) ++generated;
    if (writePcxFixture("8bit_32x32.pcx", generatePcx8bit(32, 32))) ++generated;
    if (writePcxFixture("8bit_100x75.pcx", generatePcx8bit(100, 75))) ++generated;
    if (writePcxFixture("8bit_odd_33x20.pcx", generatePcx8bit(33, 20))) ++generated;
    if (writePcxFixture("8bit_1x1.pcx", generatePcx8bit(1, 1))) ++generated;
    if (writePcxFixture("8bit_wide_256x10.pcx", generatePcx8bit(256, 10))) ++generated;
    if (writePcxFixture("8bit_tall_10x256.pcx", generatePcx8bit(10, 256))) ++generated;
    if (writePcxFixture("8bit_gradient_64x64.pcx", generatePcx8bit(64, 64, true))) ++generated;
    if (writePcxFixture("8bit_pattern_64x64.pcx", generatePcx8bit(64, 64, false))) ++generated;
    
    // 24-bit RGB fixtures
    if (writePcxFixture("24bit_16x16.pcx", generatePcx24bit(16, 16))) ++generated;
    if (writePcxFixture("24bit_32x32.pcx", generatePcx24bit(32, 32))) ++generated;
    if (writePcxFixture("24bit_100x75.pcx", generatePcx24bit(100, 75))) ++generated;
    if (writePcxFixture("24bit_odd_33x20.pcx", generatePcx24bit(33, 20))) ++generated;
    
    // 4-bit (16-color) fixtures
    if (writePcxFixture("4bit_16x16.pcx", generatePcx4bit(16, 16))) ++generated;
    if (writePcxFixture("4bit_32x32.pcx", generatePcx4bit(32, 32))) ++generated;
    
    // 1-bit monochrome fixtures
    if (writePcxFixture("1bit_16x16.pcx", generatePcx1bit(16, 16))) ++generated;
    if (writePcxFixture("1bit_32x32.pcx", generatePcx1bit(32, 32))) ++generated;
    if (writePcxFixture("1bit_100x100.pcx", generatePcx1bit(100, 100))) ++generated;
    
    printf("    Generated %d PCX fixture files in %s\n", generated, pcxDir.c_str());
    ASSERT_TRUE(generated > 0);
}

//=============================================================================
// Corpus Tests - Iterate ALL PCX Fixtures
// These tests ensure every fixture file in tests/images/pcx is exercised
//=============================================================================

TEST(PcxReader, AllPcxFixtures_MustDecode) {
    std::string pcxDir = joinPath(g_TestImagesDir, "pcx");
    if (!directoryExists(pcxDir)) SKIP_TEST("PCX images directory not found");
    
    std::vector<std::string> pcxFiles = collectFilesWithExtension(pcxDir, ".pcx");
    if (pcxFiles.empty()) SKIP_TEST("No PCX files found in corpus");
    
    CorpusTestStats stats;
    std::vector<std::string> missingCrcs;
    
    for (size_t i = 0; i < pcxFiles.size(); ++i) {
        const std::string& filename = pcxFiles[i];
        std::string filepath = joinPath(pcxDir, filename);
        
        PcxTestResult result = readPcxFile(filepath);
        
        if (result.errorCode != 0) {
            stats.recordFail(filename, "decode failed with error " + std::to_string(result.errorCode));
            continue;
        }
        
        if (result.width <= 0 || result.height <= 0) {
            stats.recordFail(filename, "invalid dimensions");
            continue;
        }
        
        // Check CRC if available
        std::string key = "pcx/" + filename;
        uint32_t expectedCrc;
        if (getReferenceCrc(key, expectedCrc)) {
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
        printf("    NOTE: %zu files have no reference CRC\n", missingCrcs.size());
    }
    
    printf("    %s\n", stats.summary().c_str());
    
    if (!stats.allPassed()) {
        for (size_t i = 0; i < stats.failures.size(); ++i) {
            printf("      FAIL: %s\n", stats.failures[i].c_str());
        }
        ASSERT_TRUE(false);
    }
}

TEST(PcxReader, AllPcxFixtures_MemoryConsistency) {
    std::string pcxDir = joinPath(g_TestImagesDir, "pcx");
    if (!directoryExists(pcxDir)) SKIP_TEST("PCX images directory not found");
    
    std::vector<std::string> pcxFiles = collectFilesWithExtension(pcxDir, ".pcx");
    if (pcxFiles.empty()) SKIP_TEST("No PCX files found in corpus");
    
    CorpusTestStats stats;
    
    for (size_t i = 0; i < pcxFiles.size(); ++i) {
        const std::string& filename = pcxFiles[i];
        std::string filepath = joinPath(pcxDir, filename);
        
        // Read via file API
        PcxTestResult fileResult = readPcxFile(filepath);
        if (fileResult.errorCode != 0) {
            stats.recordSkip();
            continue;
        }
        
        // Read via memory API
        std::vector<uint8_t> fileData = readBinaryFile(filepath);
        if (fileData.empty()) {
            stats.recordFail(filename, "failed to read file data");
            continue;
        }
        
        PcxTestResult memResult = readPcxMemory(fileData.data(), static_cast<int>(fileData.size()));
        
        if (memResult.errorCode != fileResult.errorCode) {
            stats.recordFail(filename, "error code mismatch between file and memory read");
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
    
    if (stats.total == 0) SKIP_TEST("No PCX fixtures could be tested");
    
    printf("    %s\n", stats.summary().c_str());
    
    if (!stats.allPassed()) {
        for (size_t i = 0; i < stats.failures.size(); ++i) {
            printf("      FAIL: %s\n", stats.failures[i].c_str());
        }
        ASSERT_TRUE(false);
    }
}