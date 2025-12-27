#include "CKAll.h"

#include "BmpReader.h"
#include "TgaReader.h"
#include "PcxReader.h"

#ifdef CK_LIB
#define RegisterBehaviorDeclarations Register_ImageReader_BehaviorDeclarations
#define InitInstance _ImageReader_InitInstance
#define ExitInstance _ImageReader_ExitInstance
#define CKGetPluginInfoCount CKGet_ImageReader_PluginInfoCount
#define CKGetPluginInfo CKGet_ImageReader_PluginInfo
#define g_PluginInfo g_ImageReader_PluginInfo
#define CKGetReader CKGet_ImageReader_Reader
#else
#define RegisterBehaviorDeclarations RegisterBehaviorDeclarations
#define InitInstance InitInstance
#define ExitInstance ExitInstance
#define CKGetPluginInfoCount CKGetPluginInfoCount
#define CKGetPluginInfo CKGetPluginInfo
#define g_PluginInfo g_PluginInfo
#define CKGetReader CKGetReader
#endif

#define READER_INDEX_BMP 0
#define READER_INDEX_TGA 1
#define READER_INDEX_PCX 2
#define READER_COUNT 3
CKPluginInfo g_PluginInfo[READER_COUNT];

#define READER_VERSION 0x00000001

PLUGIN_EXPORT CKDataReader *CKGetReader(int pos)
{
    // Return the appropriate reader based on index
    // This matches the original binary behavior at 0x24502a50
    switch (pos)
    {
    case READER_INDEX_BMP:
        return new BmpReader;
    case READER_INDEX_TGA:
        return new TgaReader;
    case READER_INDEX_PCX:
        return new PcxReader;
    default:
        return NULL;
    }
}

PLUGIN_EXPORT int CKGetPluginInfoCount()
{
    return READER_COUNT;
}

PLUGIN_EXPORT CKPluginInfo *CKGetPluginInfo(int index)
{
    g_PluginInfo[0].m_GUID = BMPREADER_GUID;
    g_PluginInfo[0].m_Version = READER_VERSION;
    g_PluginInfo[0].m_Description = "Windows Bitmap";
    g_PluginInfo[0].m_Summary = "Windows Bitmap";
    g_PluginInfo[0].m_Extension = "Bmp";
    g_PluginInfo[0].m_Author = "Virtools";
    g_PluginInfo[0].m_InitInstanceFct = NULL;
    g_PluginInfo[0].m_ExitInstanceFct = NULL;
    g_PluginInfo[0].m_Type = CKPLUGIN_BITMAP_READER;

    g_PluginInfo[1].m_GUID = TGAREADER_GUID;
    g_PluginInfo[1].m_Version = READER_VERSION;
    g_PluginInfo[1].m_Description = "Truevision Targa";
    g_PluginInfo[1].m_Summary = "Targa";
    g_PluginInfo[1].m_Extension = "Tga";
    g_PluginInfo[1].m_Author = "Virtools";
    g_PluginInfo[1].m_InitInstanceFct = NULL;
    g_PluginInfo[1].m_ExitInstanceFct = NULL;
    g_PluginInfo[1].m_Type = CKPLUGIN_BITMAP_READER;

    g_PluginInfo[2].m_GUID = PCXREADER_GUID;
    g_PluginInfo[2].m_Version = READER_VERSION;
    g_PluginInfo[2].m_Description = "ZSoft PCX";
    g_PluginInfo[2].m_Summary = "PCX";
    g_PluginInfo[2].m_Extension = "Pcx";
    g_PluginInfo[2].m_Author = "Virtools";
    g_PluginInfo[2].m_InitInstanceFct = NULL;
    g_PluginInfo[2].m_ExitInstanceFct = NULL;
    g_PluginInfo[2].m_Type = CKPLUGIN_BITMAP_READER;

    return &g_PluginInfo[index];
}
