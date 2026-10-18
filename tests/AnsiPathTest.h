#ifndef CK_ANSI_PATH_TEST_H
#define CK_ANSI_PATH_TEST_H

#include <gtest/gtest.h>
#include "CKAll.h"
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#endif

// Set up and inspect native files independently of the SDK path functions.
static XBOOL EncodeAnsiTestPath(const char *native, XString &ansi) {
#ifdef _WIN32
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, native, -1, NULL, 0);
    if (!length) return FALSE;
    XArray<wchar_t> wide;
    wide.Resize(length);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, native, -1, wide.Begin(), length)) return FALSE;
    UINT page = GetACP();
    BOOL substituted = FALSE;
    BOOL *used = page == CP_UTF8 ? NULL : &substituted;
    DWORD flags = page == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    length = WideCharToMultiByte(page, flags, wide.Begin(), -1, NULL, 0, NULL, used);
    if (!length || substituted) return FALSE;
    XArray<char> bytes;
    bytes.Resize(length);
    if (!WideCharToMultiByte(page, flags, wide.Begin(), -1, bytes.Begin(), length, NULL, used) || substituted) return FALSE;
    ansi = bytes.Begin();
#else
    ansi = native;
#endif
    return TRUE;
}

class AnsiTestEnvironment {
public:
    AnsiTestEnvironment(const char *name, const char *native) : Name(name), HadValue(FALSE) {
#ifdef _WIN32
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, NULL, 0);
        WideName.Resize(length);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, WideName.Begin(), length);
        SetLastError(ERROR_SUCCESS);
        DWORD count = GetEnvironmentVariableW(WideName.Begin(), NULL, 0);
        if (count) {
            Previous.Resize((int)count);
            HadValue = GetEnvironmentVariableW(WideName.Begin(), Previous.Begin(), count) < count;
        } else if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
            Previous.Resize(1); Previous[0] = 0; HadValue = TRUE;
        }
        length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, native, -1, NULL, 0);
        XArray<wchar_t> value;
        value.Resize(length);
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, native, -1, value.Begin(), length);
        EXPECT_TRUE(SetEnvironmentVariableW(WideName.Begin(), value.Begin()));
#else
        const char *old = getenv(name);
        if (old) { Previous = old; HadValue = TRUE; }
        EXPECT_EQ(0, setenv(name, native, 1));
#endif
    }
    ~AnsiTestEnvironment() {
#ifdef _WIN32
        SetEnvironmentVariableW(WideName.Begin(), HadValue ? Previous.Begin() : NULL);
#else
        if (HadValue) setenv(Name.CStr(), Previous.CStr(), 1);
        else unsetenv(Name.CStr());
#endif
    }
private:
    XString Name;
    XBOOL HadValue;
#ifdef _WIN32
    XArray<wchar_t> WideName, Previous;
#else
    XString Previous;
#endif
};

static void RemoveAnsiTestDirectory(const char *directory);
static SDL_EnumerationResult SDLCALL RemoveAnsiTestEntry(void *, const char *directory, const char *name) {
    XString path(directory); path << name;
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path.CStr(), &info) && info.type == SDL_PATHTYPE_DIRECTORY)
        RemoveAnsiTestDirectory(path.CStr());
    else
        SDL_RemovePath(path.CStr());
    return SDL_ENUM_CONTINUE;
}
static void RemoveAnsiTestDirectory(const char *directory) {
    SDL_EnumerateDirectory(directory, RemoveAnsiTestEntry, NULL);
    SDL_RemovePath(directory);
}

class CKAnsiPathTest : public ::testing::Test {
protected:
    CKContext *Context = NULL;
    XString Root, NativeDirectory, Directory;
    void SetUp() override {
        ASSERT_EQ(CK_OK, CKStartUp());
        char suffix[96];
        snprintf(suffix, sizeof(suffix), "CKAnsiPathTest-%llu/", (unsigned long long)SDL_GetPerformanceCounter());
        Root = SDL_GetBasePath(); Root << suffix;
        NativeDirectory = Root; NativeDirectory << u8"\u4E2D\u6587";
        ASSERT_TRUE(SDL_CreateDirectory(NativeDirectory.CStr()));
    }
    void TearDown() override {
        if (Context) CKCloseContext(Context);
        CKShutdown();
        RemoveAnsiTestDirectory(Root.CStr());
    }
    XBOOL PrepareDirectory(const char *name = NULL) {
        if (name) {
            NativeDirectory = Root; NativeDirectory << name;
            if (!SDL_CreateDirectory(NativeDirectory.CStr())) return FALSE;
        }
        XString native = NativeDirectory;
#ifdef _WIN32
        for (char *p = native.Str(); *p; ++p)
            if (*p == '/') *p = '\\';
#endif
        XBOOL representable = EncodeAnsiTestPath(native.CStr(), Directory);
#ifdef _WIN32
        RecordProperty("AnsiCodePage", (int)GetACP());
#else
        EXPECT_TRUE(representable);
#endif
        return representable;
    }
    void CreateContext() { ASSERT_EQ(CK_OK, CKCreateContext(&Context, NULL, 0, 0)); ASSERT_NE(nullptr, Context); }
    void CloseContext() { ASSERT_EQ(CK_OK, CKCloseContext(Context)); Context = NULL; }
    static void WriteNative(const char *path, const void *bytes, size_t size) {
        SDL_IOStream *file = SDL_IOFromFile(path, "wb");
        ASSERT_NE(nullptr, file) << SDL_GetError();
        EXPECT_EQ(size, SDL_WriteIO(file, bytes, size));
        EXPECT_TRUE(SDL_CloseIO(file));
    }
    static void ExpectNativeBytes(const char *path, const void *expected, size_t size) {
        size_t count = 0;
        void *bytes = SDL_LoadFile(path, &count);
        ASSERT_NE(nullptr, bytes) << SDL_GetError();
        EXPECT_EQ(size, count);
        if (size == count) EXPECT_EQ(0, memcmp(expected, bytes, size));
        SDL_free(bytes);
    }
};

#endif // CK_ANSI_PATH_TEST_H
