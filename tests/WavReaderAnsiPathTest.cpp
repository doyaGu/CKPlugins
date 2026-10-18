#include "AnsiPathTest.h"

static void WavePathWord(unsigned char *bytes, int offset, unsigned int value, int size = 4) {
    for (int i = 0; i < size; ++i) bytes[offset + i] = (unsigned char)(value >> (8 * i));
}

static void MakeAnsiPathWave(unsigned char (&bytes)[52]) {
    memset(bytes, 0, sizeof(bytes));
    memcpy(bytes, "RIFF", 4); WavePathWord(bytes, 4, 44); memcpy(bytes + 8, "WAVEfmt ", 8);
    WavePathWord(bytes, 16, 16); WavePathWord(bytes, 20, 1, 2); WavePathWord(bytes, 22, 1, 2);
    WavePathWord(bytes, 24, 8000); WavePathWord(bytes, 28, 16000); WavePathWord(bytes, 32, 2, 2);
    WavePathWord(bytes, 34, 16, 2); memcpy(bytes + 36, "data", 4); WavePathWord(bytes, 40, 8);
    const unsigned char samples[] = {0, 0, 0xFF, 0x7F, 0, 0x80, 0x34, 0x12};
    memcpy(bytes + 44, samples, sizeof(samples));
}

struct AnsiPathSoundReader {
    CKSoundReader *Reader;
    AnsiPathSoundReader() {
        CKFileExtension ext("wav"); Reader = CKGetPluginManager()->GetSoundReader(ext);
    }
    ~AnsiPathSoundReader() { if (Reader) Reader->Release(); }
};

static void ExpectAnsiPathWaveSamples(CKSoundReader *reader) {
    CKWaveFormat format;
    ASSERT_EQ(CK_OK, reader->GetWaveFormat(&format));
    EXPECT_EQ(1, format.nChannels); EXPECT_EQ(8000u, format.nSamplesPerSec); EXPECT_EQ(16, format.wBitsPerSample);
    EXPECT_EQ(8, reader->GetDataSize());
    ASSERT_EQ(CK_OK, reader->Seek(0)); ASSERT_EQ(CK_OK, reader->Decode());
    CKBYTE *buffer = NULL; int size = 0;
    ASSERT_EQ(CK_OK, reader->GetDataBuffer(&buffer, &size)); ASSERT_NE(nullptr, buffer);
    const unsigned char expected[] = {0, 0, 0xFF, 0x7F, 0, 0x80, 0x34, 0x12};
    ASSERT_EQ(sizeof(expected), size); EXPECT_EQ(0, memcmp(expected, buffer, size));
    ASSERT_EQ(CK_OK, reader->Seek(2)); ASSERT_EQ(CK_OK, reader->Decode());
    ASSERT_EQ(CK_OK, reader->GetDataBuffer(&buffer, &size)); ASSERT_NE(nullptr, buffer);
    ASSERT_EQ(sizeof(expected) - 2, size); EXPECT_EQ(0, memcmp(expected + 2, buffer, size));
}

class WavReaderAnsiPathTest : public CKAnsiPathTest {
protected:
    void SetUp() override {
        CKAnsiPathTest::SetUp(); if (HasFatalFailure()) return;
        ASSERT_EQ(CK_OK, CKGetPluginManager()->RegisterPlugin((char *)READER_MODULE_FILE));
    }
};

TEST_F(WavReaderAnsiPathTest, ChineseFileStreamingMatchesMemoryAndSupportsSeekAndReopen) {
    if (!PrepareDirectory()) return;
    unsigned char bytes[52]; MakeAnsiPathWave(bytes);
    XString native = NativeDirectory; native << u8"/\u58F0\u97F3.wav";
    WriteNative(native.CStr(), bytes, sizeof(bytes));
    XString filename; ASSERT_TRUE(EncodeAnsiTestPath(native.CStr(), filename));
    {
        AnsiPathSoundReader reader; ASSERT_NE(nullptr, reader.Reader);
        ASSERT_EQ(CK_OK, reader.Reader->ReadMemory(bytes, sizeof(bytes))); ExpectAnsiPathWaveSamples(reader.Reader);
        for (int repeat = 0; repeat < 3; ++repeat) {
            ASSERT_EQ(CK_OK, reader.Reader->OpenFile(filename.Str())); ExpectAnsiPathWaveSamples(reader.Reader);
        }
        XString invalid = Directory; invalid << "/invalid-\x81.wav";
        EXPECT_NE(CK_OK, reader.Reader->OpenFile(invalid.Str()));
        ASSERT_EQ(CK_OK, reader.Reader->OpenFile(filename.Str())); ExpectAnsiPathWaveSamples(reader.Reader);
#ifdef _WIN32
        if (filename != native) EXPECT_NE(CK_OK, reader.Reader->OpenFile(native.Str()));
#endif
    }
    ExpectNativeBytes(native.CStr(), bytes, sizeof(bytes));
    EXPECT_TRUE(SDL_RemovePath(native.CStr()));
}

#ifndef _WIN32
TEST_F(WavReaderAnsiPathTest, NativeWesternFilenameKeepsSoundSamples) {
    ASSERT_TRUE(PrepareDirectory(u8"Fran\u00E7ois"));
    unsigned char bytes[52]; MakeAnsiPathWave(bytes);
    XString filename = NativeDirectory; filename << u8"/caf\u00E9.wav";
    WriteNative(filename.CStr(), bytes, sizeof(bytes));
    {
        AnsiPathSoundReader reader; ASSERT_NE(nullptr, reader.Reader);
        for (int repeat = 0; repeat < 3; ++repeat) {
            ASSERT_EQ(CK_OK, reader.Reader->OpenFile(filename.Str()));
            ExpectAnsiPathWaveSamples(reader.Reader);
        }
    }
    ExpectNativeBytes(filename.CStr(), bytes, sizeof(bytes));
    EXPECT_TRUE(SDL_RemovePath(filename.CStr()));
}
#endif

TEST_F(WavReaderAnsiPathTest, WaveExtractedFromCompositionUsesChineseTemp) {
    if (!PrepareDirectory()) return;
    AnsiTestEnvironment tmp("TMP", NativeDirectory.CStr()), temp("TEMP", NativeDirectory.CStr()), tmpdir("TMPDIR", NativeDirectory.CStr());
    unsigned char bytes[52]; MakeAnsiPathWave(bytes);
    XString native = NativeDirectory; native << u8"/\u58F0\u97F3.wav";
    WriteNative(native.CStr(), bytes, sizeof(bytes));
    XString source; ASSERT_TRUE(EncodeAnsiTestPath(native.CStr(), source));
    XString composition; ASSERT_TRUE(VxMakePath(composition, Directory.CStr(), "sound.cmo"));
    CreateContext(); ASSERT_NE(nullptr, Context);
    {
        CKFile file(Context); ASSERT_EQ(CK_OK, file.StartSave(composition.Str()));
        ASSERT_TRUE(file.IncludeFile(source.Str())); ASSERT_EQ(CK_OK, file.EndSave());
    }
    ASSERT_TRUE(SDL_RemovePath(native.CStr())); CloseContext(); CreateContext(); ASSERT_NE(nullptr, Context);
    {
        CKFile file(Context); ASSERT_EQ(CK_OK, file.Load(composition.Str(), NULL));
    }
    XString name; ASSERT_TRUE(EncodeAnsiTestPath(u8"\u58F0\u97F3.wav", name));
    XString filename;
    ASSERT_TRUE(VxMakePath(filename, Context->GetPathManager()->GetVirtoolsTemporaryFolder().CStr(), name.CStr()));
    AnsiPathSoundReader reader; ASSERT_NE(nullptr, reader.Reader);
    ASSERT_EQ(CK_OK, reader.Reader->OpenFile(filename.Str())); ExpectAnsiPathWaveSamples(reader.Reader);
}
