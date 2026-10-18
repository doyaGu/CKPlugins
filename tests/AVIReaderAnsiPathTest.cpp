#include "AnsiPathTest.h"

static void AviPathWord(unsigned char *bytes, int offset, unsigned int value, int size = 4) {
    for (int i = 0; i < size; ++i) bytes[offset + i] = (unsigned char)(value >> (8 * i));
}

static void MakeAnsiPathMovie(XArray<unsigned char> &bytes) {
    // Two independent 1x1 BGR frames and their classic idx1 records.
    bytes.Resize(288); memset(bytes.Begin(), 0, bytes.Size());
    unsigned char *data = bytes.Begin();
    memcpy(data, "RIFF", 4); AviPathWord(data, 4, 280); memcpy(data + 8, "AVI ", 4);
    memcpy(data + 12, "LIST", 4); AviPathWord(data, 16, 192); memcpy(data + 20, "hdrl", 4);
    memcpy(data + 24, "avih", 4); AviPathWord(data, 28, 56); AviPathWord(data, 32, 33333);
    AviPathWord(data, 44, 0x10); AviPathWord(data, 48, 2); AviPathWord(data, 56, 1);
    AviPathWord(data, 64, 1); AviPathWord(data, 68, 1);
    memcpy(data + 88, "LIST", 4); AviPathWord(data, 92, 116); memcpy(data + 96, "strl", 4);
    memcpy(data + 100, "strh", 4); AviPathWord(data, 104, 56); memcpy(data + 108, "vids", 4);
    AviPathWord(data, 128, 1); AviPathWord(data, 132, 30); AviPathWord(data, 140, 2); AviPathWord(data, 144, 4);
    AviPathWord(data, 160, 1, 2); AviPathWord(data, 162, 1, 2);
    memcpy(data + 164, "strf", 4); AviPathWord(data, 168, 40); AviPathWord(data, 172, 40);
    AviPathWord(data, 176, 1); AviPathWord(data, 180, 1); AviPathWord(data, 184, 1, 2);
    AviPathWord(data, 186, 24, 2); AviPathWord(data, 192, 4);
    memcpy(data + 212, "LIST", 4); AviPathWord(data, 216, 28); memcpy(data + 220, "movi", 4);
    memcpy(data + 224, "00db", 4); AviPathWord(data, 228, 4);
    data[232] = 0x11; data[233] = 0x22; data[234] = 0x33;
    memcpy(data + 236, "00db", 4); AviPathWord(data, 240, 4);
    data[244] = 0x44; data[245] = 0x55; data[246] = 0x66;
    memcpy(data + 248, "idx1", 4); AviPathWord(data, 252, 32);
    memcpy(data + 256, "00db", 4); AviPathWord(data, 260, 0x10); AviPathWord(data, 264, 4); AviPathWord(data, 268, 4);
    memcpy(data + 272, "00db", 4); AviPathWord(data, 276, 0x10); AviPathWord(data, 280, 16); AviPathWord(data, 284, 4);
}

struct AnsiPathMovieReader {
    CKMovieReader *Reader;
    AnsiPathMovieReader() {
        CKFileExtension ext("avi"); Reader = CKGetPluginManager()->GetMovieReader(ext);
    }
    ~AnsiPathMovieReader() { if (Reader) Reader->Release(); }
};

static void ExpectAnsiPathMovieFrames(CKMovieReader *reader) {
    ASSERT_EQ(2, reader->GetMovieFrameCount());
    const unsigned char frames[][4] = {{0x11, 0x22, 0x33, 0xFF}, {0x44, 0x55, 0x66, 0xFF}};
    const int order[] = {1, 0, 1};
    for (int i = 0; i < 3; ++i) {
        CKMovieProperties *properties = NULL;
        ASSERT_EQ(CK_OK, reader->ReadFrame(order[i], &properties));
        ASSERT_NE(nullptr, properties); ASSERT_NE(nullptr, properties->m_Data);
        EXPECT_EQ(1, properties->m_Format.Width); EXPECT_EQ(1, properties->m_Format.Height);
        EXPECT_EQ(0, memcmp(frames[order[i]], properties->m_Data, 4));
    }
}

class AVIReaderAnsiPathTest : public CKAnsiPathTest {
protected:
    void SetUp() override {
        CKAnsiPathTest::SetUp(); if (HasFatalFailure()) return;
        ASSERT_EQ(CK_OK, CKGetPluginManager()->RegisterPlugin((char *)READER_MODULE_FILE));
    }
};

TEST_F(AVIReaderAnsiPathTest, ChineseMovieCanBeReopenedAndReadOutOfOrder) {
    if (!PrepareDirectory()) return;
    XArray<unsigned char> bytes; MakeAnsiPathMovie(bytes);
    XString native = NativeDirectory; native << u8"/\u89C6\u9891.avi";
    WriteNative(native.CStr(), bytes.Begin(), bytes.Size());
    XString filename; ASSERT_TRUE(EncodeAnsiTestPath(native.CStr(), filename));
    {
        AnsiPathMovieReader reader; ASSERT_NE(nullptr, reader.Reader);
        for (int repeat = 0; repeat < 3; ++repeat) {
            ASSERT_EQ(CK_OK, reader.Reader->OpenFile(filename.Str())); ExpectAnsiPathMovieFrames(reader.Reader);
        }
        XString invalid = Directory; invalid << "/invalid-\x81.avi";
        EXPECT_NE(CK_OK, reader.Reader->OpenFile(invalid.Str()));
        ASSERT_EQ(CK_OK, reader.Reader->OpenFile(filename.Str())); ExpectAnsiPathMovieFrames(reader.Reader);
#ifdef _WIN32
        if (filename != native) EXPECT_NE(CK_OK, reader.Reader->OpenFile(native.Str()));
#endif
    }
    EXPECT_TRUE(SDL_RemovePath(native.CStr()));
}

#ifndef _WIN32
TEST_F(AVIReaderAnsiPathTest, NativeWesternFilenameKeepsMovieFrames) {
    ASSERT_TRUE(PrepareDirectory(u8"Fran\u00E7ois"));
    XArray<unsigned char> bytes; MakeAnsiPathMovie(bytes);
    XString filename = NativeDirectory; filename << u8"/caf\u00E9.avi";
    WriteNative(filename.CStr(), bytes.Begin(), bytes.Size());
    {
        AnsiPathMovieReader reader; ASSERT_NE(nullptr, reader.Reader);
        for (int repeat = 0; repeat < 3; ++repeat) {
            ASSERT_EQ(CK_OK, reader.Reader->OpenFile(filename.Str()));
            ExpectAnsiPathMovieFrames(reader.Reader);
        }
    }
    ExpectNativeBytes(filename.CStr(), bytes.Begin(), bytes.Size());
    EXPECT_TRUE(SDL_RemovePath(filename.CStr()));
}
#endif

TEST_F(AVIReaderAnsiPathTest, MovieExtractedFromCompositionUsesChineseTemp) {
    if (!PrepareDirectory()) return;
    AnsiTestEnvironment tmp("TMP", NativeDirectory.CStr()), temp("TEMP", NativeDirectory.CStr()), tmpdir("TMPDIR", NativeDirectory.CStr());
    XArray<unsigned char> bytes; MakeAnsiPathMovie(bytes);
    XString native = NativeDirectory; native << u8"/\u89C6\u9891.avi";
    WriteNative(native.CStr(), bytes.Begin(), bytes.Size());
    XString source; ASSERT_TRUE(EncodeAnsiTestPath(native.CStr(), source));
    XString composition; ASSERT_TRUE(VxMakePath(composition, Directory.CStr(), "movie.cmo"));
    CreateContext(); ASSERT_NE(nullptr, Context);
    {
        CKFile file(Context); ASSERT_EQ(CK_OK, file.StartSave(composition.Str()));
        ASSERT_TRUE(file.IncludeFile(source.Str())); ASSERT_EQ(CK_OK, file.EndSave());
    }
    ASSERT_TRUE(SDL_RemovePath(native.CStr())); CloseContext(); CreateContext(); ASSERT_NE(nullptr, Context);
    {
        CKFile file(Context); ASSERT_EQ(CK_OK, file.Load(composition.Str(), NULL));
    }
    XString name; ASSERT_TRUE(EncodeAnsiTestPath(u8"\u89C6\u9891.avi", name));
    XString filename;
    ASSERT_TRUE(VxMakePath(filename, Context->GetPathManager()->GetVirtoolsTemporaryFolder().CStr(), name.CStr()));
    AnsiPathMovieReader reader; ASSERT_NE(nullptr, reader.Reader);
    ASSERT_EQ(CK_OK, reader.Reader->OpenFile(filename.Str())); ExpectAnsiPathMovieFrames(reader.Reader);
}
