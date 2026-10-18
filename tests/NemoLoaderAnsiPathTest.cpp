#include "AnsiPathTest.h"

class NemoLoaderAnsiPathTest : public CKAnsiPathTest {
protected:
    void SetUp() override {
        CKAnsiPathTest::SetUp();
        if (HasFatalFailure()) return;
        ASSERT_EQ(CK_OK, CKGetPluginManager()->RegisterPlugin((char *)READER_MODULE_FILE));
    }
};

TEST_F(NemoLoaderAnsiPathTest, ContextLoadsChineseCompositionThroughTheRegisteredReader) {
    if (!PrepareDirectory()) return;
    CreateContext(); ASSERT_NE(nullptr, Context);
    const char name[] = "\xD6\xD0\xCE\xC4";
    CKObject *object = Context->CreateObject(CKCID_BEOBJECT, (char *)name); ASSERT_NE(nullptr, object);
    XString native = NativeDirectory; native << u8"/\u7EC4\u5408.cmo";
    XString filename; ASSERT_TRUE(EncodeAnsiTestPath(native.CStr(), filename));
    {
        CKFile file(Context); ASSERT_EQ(CK_OK, file.StartSave(filename.Str()));
        file.SaveObject(object); ASSERT_EQ(CK_OK, file.EndSave());
    }
    CloseContext(); CreateContext(); ASSERT_NE(nullptr, Context);
    CKObjectArray objects;
    ASSERT_EQ(CK_OK, Context->Load(filename.Str(), &objects));
    ASSERT_EQ(1, objects.GetCount()); objects.Reset();
    ASSERT_NE(nullptr, objects.GetData(Context));
    EXPECT_STREQ(name, objects.GetData(Context)->GetName());
    EXPECT_STREQ(filename.CStr(), Context->GetLastFileLoaded());
}

TEST_F(NemoLoaderAnsiPathTest, ContextSaveReopenUnderChineseTempPreservesAnsiObjectNames) {
    if (!PrepareDirectory()) return;
    AnsiTestEnvironment tmp("TMP", NativeDirectory.CStr()), temp("TEMP", NativeDirectory.CStr()), tmpdir("TMPDIR", NativeDirectory.CStr());
    CreateContext(); ASSERT_NE(nullptr, Context);
    const char name[] = "\xD6\xD0\xCE\xC4";
    CKObject *object = Context->CreateObject(CKCID_BEOBJECT, (char *)name); ASSERT_NE(nullptr, object);
    XString native = NativeDirectory; native << u8"/\u4FDD\u5B58.cmo";
    XString filename; ASSERT_TRUE(EncodeAnsiTestPath(native.CStr(), filename));
    {
        CKObjectArray objects; objects.InsertRear(object->GetID());
        ASSERT_EQ(CK_OK, Context->Save(filename.Str(), &objects, CK_STATESAVE_ALL));
    }
    SDL_PathInfo info; ASSERT_TRUE(SDL_GetPathInfo(native.CStr(), &info)); EXPECT_GT(info.size, 64u);
    CloseContext(); CreateContext(); ASSERT_NE(nullptr, Context);
    CKObjectArray objects;
    ASSERT_EQ(CK_OK, Context->Load(filename.Str(), &objects));
    ASSERT_EQ(1, objects.GetCount()); objects.Reset();
    ASSERT_NE(nullptr, objects.GetData(Context)); EXPECT_STREQ(name, objects.GetData(Context)->GetName());
}

class NemoSaveProbe : public CKBaseManager {
public:
    explicit NemoSaveProbe(CKContext *context)
        : CKBaseManager(context, CKGUID(0x1C5A3471, 0x21783491), (char *)"Save path probe"), Pre(0), Post(0), Data(0) {}
    CKDWORD GetValidFunctionsMask() override { return CKMANAGER_FUNC_PreSave | CKMANAGER_FUNC_PostSave; }
    CKERROR PreSave() override { ++Pre; return CK_OK; }
    CKERROR PostSave() override { ++Post; return CK_OK; }
    CKStateChunk *SaveData(CKFile *) override { ++Data; return NULL; }
    int Pre, Post, Data;
};

TEST_F(NemoLoaderAnsiPathTest, FailedStartDoesNotContinueSavingAndCanBeRetried) {
    ASSERT_TRUE(PrepareDirectory("ascii"));
    CreateContext(); ASSERT_NE(nullptr, Context);
    NemoSaveProbe *probe = new NemoSaveProbe(Context);
    ASSERT_EQ(CK_OK, Context->RegisterNewManager(probe));
    XString filename; ASSERT_TRUE(VxMakePath(filename, Directory.CStr(), "missing/saved.cmo"));
    CKObjectArray objects;
    EXPECT_EQ(CKERR_CANTWRITETOFILE, Context->Save(filename.Str(), &objects, CK_STATESAVE_ALL));
    EXPECT_EQ(1, probe->Pre); EXPECT_EQ(1, probe->Post); EXPECT_EQ(0, probe->Data);
    EXPECT_FALSE(Context->m_Saving);
    ASSERT_TRUE(VxMakePath(filename, Directory.CStr(), "retry.cmo"));
    ASSERT_EQ(CK_OK, Context->Save(filename.Str(), &objects, CK_STATESAVE_ALL));
    EXPECT_EQ(2, probe->Pre); EXPECT_EQ(2, probe->Post); EXPECT_EQ(1, probe->Data);
}

#ifndef _WIN32
TEST_F(NemoLoaderAnsiPathTest, NativeWesternFilenameUsesTheRegisteredReader) {
    ASSERT_TRUE(PrepareDirectory(u8"Fran\u00E7ois"));
    CreateContext(); ASSERT_NE(nullptr, Context);
    CKObject *object = Context->CreateObject(CKCID_BEOBJECT, (char *)"caf\xE9");
    ASSERT_NE(nullptr, object);
    XString filename = NativeDirectory; filename << u8"/caf\u00E9.cmo";
    CKObjectArray objects; objects.InsertRear(object->GetID());
    ASSERT_EQ(CK_OK, Context->Save(filename.Str(), &objects, CK_STATESAVE_ALL));
    CloseContext(); CreateContext(); ASSERT_NE(nullptr, Context);
    CKObjectArray loaded;
    ASSERT_EQ(CK_OK, Context->Load(filename.Str(), &loaded));
    ASSERT_EQ(1, loaded.GetCount()); loaded.Reset();
    ASSERT_NE(nullptr, loaded.GetData(Context));
    EXPECT_STREQ("caf\xE9", loaded.GetData(Context)->GetName());
    EXPECT_STREQ(filename.CStr(), Context->GetLastFileLoaded());
}
#endif
