#include "AnsiPathTest.h"

static void ImageWord(unsigned char *bytes, int offset, unsigned int value, int size = 4) {
    for (int i = 0; i < size; ++i) bytes[offset + i] = (unsigned char)(value >> (8 * i));
}

static void MakePathTestImage(const char *extension, XArray<unsigned char> &bytes) {
    if (strcmp(extension, "bmp") == 0) {
        bytes.Resize(62); memset(bytes.Begin(), 0, bytes.Size());
        bytes[0] = 'B'; bytes[1] = 'M'; ImageWord(bytes.Begin(), 2, 62); ImageWord(bytes.Begin(), 10, 54);
        ImageWord(bytes.Begin(), 14, 40); ImageWord(bytes.Begin(), 18, 2); ImageWord(bytes.Begin(), 22, 1);
        ImageWord(bytes.Begin(), 26, 1, 2); ImageWord(bytes.Begin(), 28, 24, 2); ImageWord(bytes.Begin(), 34, 8);
        const unsigned char pixels[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
        memcpy(bytes.Begin() + 54, pixels, sizeof(pixels));
    } else if (strcmp(extension, "tga") == 0) {
        bytes.Resize(24); memset(bytes.Begin(), 0, bytes.Size());
        bytes[2] = 2; ImageWord(bytes.Begin(), 12, 2, 2); ImageWord(bytes.Begin(), 14, 1, 2); bytes[16] = 24;
        const unsigned char pixels[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
        memcpy(bytes.Begin() + 18, pixels, sizeof(pixels));
    } else {
        bytes.Resize(134); memset(bytes.Begin(), 0, bytes.Size());
        bytes[0] = 10; bytes[1] = 5; bytes[3] = 8; ImageWord(bytes.Begin(), 8, 1, 2);
        bytes[65] = 3; ImageWord(bytes.Begin(), 66, 2, 2); ImageWord(bytes.Begin(), 68, 1, 2);
        const unsigned char planes[] = {0x33, 0x66, 0x22, 0x55, 0x11, 0x44};
        memcpy(bytes.Begin() + 128, planes, sizeof(planes));
    }
}

struct PathTestBitmapReader {
    CKBitmapReader *Reader;
    explicit PathTestBitmapReader(const char *extension) {
        CKFileExtension ext(extension); Reader = CKGetPluginManager()->GetBitmapReader(ext);
    }
    ~PathTestBitmapReader() { if (Reader) Reader->Release(); }
};

static void ExpectImagePixels(CKBitmapProperties *properties) {
    ASSERT_NE(nullptr, properties); ASSERT_NE(nullptr, properties->m_Data);
    EXPECT_EQ(2, properties->m_Format.Width); EXPECT_EQ(1, properties->m_Format.Height);
    EXPECT_EQ(32, properties->m_Format.BitsPerPixel);
    const unsigned char expected[] = {0x11, 0x22, 0x33, 0xFF, 0x44, 0x55, 0x66, 0xFF};
    EXPECT_EQ(0, memcmp(expected, properties->m_Data, sizeof(expected)));
}

static void ReleasePathTestPixels(CKBitmapReader *reader, CKBitmapProperties *properties) {
    reader->ReleaseMemory(properties->m_Data); properties->m_Data = NULL; properties->m_Format.Image = NULL;
    if (properties->m_Format.ColorMap) {
        reader->ReleaseMemory(properties->m_Format.ColorMap); properties->m_Format.ColorMap = NULL;
    }
}

class ImageReaderAnsiPathTest : public CKAnsiPathTest {
protected:
    void SetUp() override {
        CKAnsiPathTest::SetUp(); if (HasFatalFailure()) return;
        ASSERT_EQ(CK_OK, CKGetPluginManager()->RegisterPlugin((char *)READER_MODULE_FILE));
    }
    void ReadImage(const char *extension, const char *stem = u8"\u56FE\u7247") {
        if (!PrepareDirectory()) return;
        XArray<unsigned char> bytes; MakePathTestImage(extension, bytes);
        XString native = NativeDirectory; native << '/' << stem << '.' << extension;
        WriteNative(native.CStr(), bytes.Begin(), bytes.Size());
        XString filename; ASSERT_TRUE(EncodeAnsiTestPath(native.CStr(), filename));
        PathTestBitmapReader reader(extension); ASSERT_NE(nullptr, reader.Reader);
        CKBitmapProperties *properties = NULL;
        ASSERT_EQ(0, reader.Reader->ReadMemory(bytes.Begin(), bytes.Size(), &properties));
        ExpectImagePixels(properties); ReleasePathTestPixels(reader.Reader, properties);
        for (int repeat = 0; repeat < 3; ++repeat) {
            ASSERT_EQ(0, reader.Reader->ReadFile(filename.Str(), &properties));
            ExpectImagePixels(properties); ReleasePathTestPixels(reader.Reader, properties);
        }
        EXPECT_TRUE(SDL_RemovePath(native.CStr()));
    }
    void SaveImage(const char *extension, const char *stem = u8"\u4FDD\u5B58") {
        if (!PrepareDirectory()) return;
        AnsiTestEnvironment tmp("TMP", NativeDirectory.CStr()), temp("TEMP", NativeDirectory.CStr()), tmpdir("TMPDIR", NativeDirectory.CStr());
        PathTestBitmapReader reader(extension); ASSERT_NE(nullptr, reader.Reader);
        CKBitmapProperties *properties = NULL; reader.Reader->GetBitmapDefaultProperties(&properties);
        ASSERT_NE(nullptr, properties);
        unsigned char pixels[] = {0x11, 0x22, 0x33, 0xFF, 0x44, 0x55, 0x66, 0xFF};
        properties->m_Format.Width = 2; properties->m_Format.Height = 1;
        properties->m_Format.BitsPerPixel = 32; properties->m_Format.BytesPerLine = 8;
        properties->m_Format.RedMask = 0x00FF0000; properties->m_Format.GreenMask = 0x0000FF00;
        properties->m_Format.BlueMask = 0x000000FF; properties->m_Format.AlphaMask = 0xFF000000;
        properties->m_Format.Image = pixels; properties->m_Data = pixels;
        void *encoded = NULL;
        int size = reader.Reader->SaveMemory(&encoded, properties);
        ASSERT_GT(size, 0); ASSERT_NE(nullptr, encoded);
        XString name; ASSERT_TRUE(EncodeAnsiTestPath(stem, name)); name << '.' << extension;
        XString filename; ASSERT_TRUE(VxMakePath(filename, VxGetTempPath().CStr(), name.CStr()));
        EXPECT_EQ(size, reader.Reader->SaveFile(filename.Str(), properties));
        XString native = NativeDirectory; native << '/' << stem << '.' << extension;
        ExpectNativeBytes(native.CStr(), encoded, size); reader.Reader->ReleaseMemory(encoded);
        properties->m_Format.Image = NULL; properties->m_Data = NULL;
        CKBitmapProperties *reopened = NULL;
        ASSERT_EQ(0, reader.Reader->ReadFile(filename.Str(), &reopened)); ExpectImagePixels(reopened);
    }
};

TEST_F(ImageReaderAnsiPathTest, BmpLoadsChineseFilenameWithoutChangingPixels) { ReadImage("bmp"); }
TEST_F(ImageReaderAnsiPathTest, TgaLoadsChineseFilenameWithoutChangingPixels) { ReadImage("tga"); }
TEST_F(ImageReaderAnsiPathTest, PcxLoadsChineseFilenameWithoutChangingPixels) { ReadImage("pcx"); }
TEST_F(ImageReaderAnsiPathTest, BmpSaveReopenUnderChineseTempMatchesMemoryEncoding) { SaveImage("bmp"); }
TEST_F(ImageReaderAnsiPathTest, TgaSaveReopenUnderChineseTempMatchesMemoryEncoding) { SaveImage("tga"); }

#ifndef _WIN32
TEST_F(ImageReaderAnsiPathTest, NativeWesternFilenamesKeepPixelsAndSavedBytes) {
    ASSERT_TRUE(PrepareDirectory(u8"Fran\u00E7ois"));
    ReadImage("bmp", u8"caf\u00E9");
    ReadImage("tga", u8"caf\u00E9");
    ReadImage("pcx", u8"caf\u00E9");
    SaveImage("bmp", u8"caf\u00E9");
    SaveImage("tga", u8"caf\u00E9");
}
#endif

TEST_F(ImageReaderAnsiPathTest, IncludedBitmapLoadsFromChineseTempThroughTheExistingBitmapApi) {
    if (!PrepareDirectory()) return;
    AnsiTestEnvironment tmp("TMP", NativeDirectory.CStr()), temp("TEMP", NativeDirectory.CStr()), tmpdir("TMPDIR", NativeDirectory.CStr());
    XArray<unsigned char> bytes; MakePathTestImage("bmp", bytes);
    XString native = NativeDirectory; native << u8"/\u56FE\u7247.bmp";
    WriteNative(native.CStr(), bytes.Begin(), bytes.Size());
    XString source; ASSERT_TRUE(EncodeAnsiTestPath(native.CStr(), source));
    XString composition; ASSERT_TRUE(VxMakePath(composition, Directory.CStr(), "images.cmo"));
    CreateContext(); ASSERT_NE(nullptr, Context);
    {
        CKFile file(Context); ASSERT_EQ(CK_OK, file.StartSave(composition.Str()));
        ASSERT_TRUE(file.IncludeFile(source.Str())); ASSERT_EQ(CK_OK, file.EndSave());
    }
    ASSERT_TRUE(SDL_RemovePath(native.CStr())); CloseContext(); CreateContext(); ASSERT_NE(nullptr, Context);
    {
        CKFile file(Context); ASSERT_EQ(CK_OK, file.Load(composition.Str(), NULL));
    }
    XString name; ASSERT_TRUE(EncodeAnsiTestPath(u8"\u56FE\u7247.bmp", name));
    XString filename;
    ASSERT_TRUE(VxMakePath(filename, Context->GetPathManager()->GetVirtoolsTemporaryFolder().CStr(), name.CStr()));
    CKBitmapData bitmap;
    ASSERT_TRUE(bitmap.LoadSlotImage(filename, 0));
    EXPECT_STREQ(filename.CStr(), bitmap.GetSlotFileName(0));
    EXPECT_EQ(0xFF332211u, bitmap.GetPixel(0, 0, 0)); EXPECT_EQ(0xFF665544u, bitmap.GetPixel(1, 0, 0));
}
