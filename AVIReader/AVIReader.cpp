#include "AVIReader.h"

#include "VxMath.h"

#ifdef CK_LIB
#define RegisterBehaviorDeclarations    Register_AviReader_BehaviorDeclarations
#define InitInstance                    _AviReader_InitInstance
#define ExitInstance                    _AviReader_ExitInstance
#define CKGetPluginInfoCount            CKGet_AviReader_PluginInfoCount
#define CKGetPluginInfo                 CKGet_AviReader_PluginInfo
#define g_PluginInfo                    g_AviReader_PluginInfo
#define CKGetReader                     CKGet_AviReader_Reader
#else
#define RegisterBehaviorDeclarations    RegisterBehaviorDeclarations
#define InitInstance                    InitInstance
#define ExitInstance                    ExitInstance
#define CKGetPluginInfoCount            CKGetPluginInfoCount
#define CKGetPluginInfo                 CKGetPluginInfo
#define g_PluginInfo                    g_PluginInfo
#define CKGetReader                     CKGetReader
#endif

CKPluginInfo g_PluginInfo;

/**********************************************
 Called by the engine when a file with the AVI
 extension is being loaded, a reader has to be
 created.
***********************************************/
CKDataReader *CKGetReader(int pos)
{
    return new AVIReader();
}

/**********************************************
Called by the engine when it parses for available
plugins. Returns the information about this plugin.
The more important being the extension for the
movie files it is able to load (in this case "Avi".
***********************************************/
CKPluginInfo *CKGetPluginInfo(int index)
{
    g_PluginInfo.m_Author = "Virtools";
    g_PluginInfo.m_Description = "Win32 AVI Movie Reader";
    g_PluginInfo.m_Extension = "Avi";
    g_PluginInfo.m_Type = CKPLUGIN_MOVIE_READER;
    g_PluginInfo.m_Version = AVI_READER_VERSION;
    g_PluginInfo.m_InitInstanceFct = NULL;
    g_PluginInfo.m_GUID = AVI_READER_GUID;
    g_PluginInfo.m_Summary = "AVI Reader";
    return &g_PluginInfo;
}

/**************************************************
 Ctor: initialize data
***************************************************/
AVIReader::AVIReader()
{
    m_Properties.m_Ext = "avi";
    m_Properties.m_ReaderGuid = AVI_READER_GUID;
    m_Properties.m_Data = NULL;

    m_Stream = NULL;
    m_Frame = NULL;
    m_FrameCount = 0;

    // AVI initialization
    AVIFileInit();
}

/*****************************************
 Destructor: Free memory and release AVI handles
******************************************/
AVIReader::~AVIReader()
{
    // if bitmap data was still present we need to release it
    m_Properties.m_Data = NULL;

    // release the AVI Data
    ReleaseAVI();
    // AVI initialization
    AVIFileExit();
}

/******************************************
 Returns information about the reader which
 is the same as given to the engine at
 initialisation.
*******************************************/
CKPluginInfo *AVIReader::GetReaderInfo()
{
    return &g_PluginInfo;
}

/*******************************************
 Release all AVI Handles
********************************************/
void AVIReader::ReleaseAVI()
{
    if (m_Frame)
        AVIStreamGetFrameClose(m_Frame);
    m_Frame = NULL;

    if (m_Stream)
        AVIStreamRelease(m_Stream);
    m_Stream = NULL;

    m_FrameCount = 0;
}

/*******************************************
 Number of frames in the movie file
*******************************************/
int AVIReader::GetMovieFrameCount()
{
    return m_FrameCount;
}

/*******************************************
 Length in Ms of the move.
*******************************************/
int AVIReader::GetMovieLength()
{
    return AVIStreamSampleToTime(m_Stream, m_FrameCount);
}

/*********************************************************
Open a .AVI file and retrieve information : Number of
frames and pixel format of the movie.
*********************************************************/
CKERROR AVIReader::OpenFile(char *name)
{
    // Try to create a AVIStream from the file , if failed return an error
    HRESULT hr = AVIStreamOpenFromFile(&m_Stream, name, streamtypeVIDEO, 0, OF_READ, NULL);
    if (hr)
        return CKMOVIEERROR_UNSUPPORTEDFILE;

    // Try to open frames with defult format or best if it failed
    m_Frame = AVIStreamGetFrameOpen(m_Stream, NULL);
    if (!m_Frame)
    {
        m_Frame = AVIStreamGetFrameOpen(m_Stream, (BITMAPINFOHEADER *)AVIGETFRAMEF_BESTDISPLAYFMT);
        if (!m_Frame)
        {
            ReleaseAVI();
            return CKMOVIEERROR_UNSUPPORTEDFILE;
        }
    }

    // Try to get the first frame of the movie
    if (void *dib = AVIStreamGetFrame(m_Frame, AVIStreamStart(m_Stream)))
    {
        BITMAPINFO *bi = (BITMAPINFO *)dib;
        BYTE *BitData = (BYTE *)bi + bi->bmiHeader.biSize + sizeof(RGBQUAD) * bi->bmiHeader.biClrUsed;
        // Store the movie Format
        m_Properties.m_Format.Width = bi->bmiHeader.biWidth;
        m_Properties.m_Format.Height = bi->bmiHeader.biHeight;
        m_Properties.m_Format.BitsPerPixel = (char)bi->bmiHeader.biBitCount;
        m_Properties.m_Format.BytesPerLine = bi->bmiHeader.biSizeImage / bi->bmiHeader.biHeight;
        VxBppToMask(m_Properties.m_Format);
        m_Properties.m_Format.AlphaMask = 0;

        // Get the number of frame in the movie
        m_FrameCount = AVIStreamLength(m_Stream);
    }
    else
    {
        ReleaseAVI();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }

    return CK_OK;
}

/*****************************************************
 + Decode a frame of the movie.
 f is the requested frame.
 mp parameters will be filled with the format of the image.
 mp.m_Data will contain a pointer to the bitmap data.
******************************************************/
CKERROR AVIReader::ReadFrame(int f, CKMovieProperties **mp)
{
    if ((DWORD)f >= (DWORD)m_FrameCount)
        return CKMOVIEERROR_GENERIC;

    void *dib = AVIStreamGetFrame(m_Frame, f + AVIStreamStart(m_Stream));
    BITMAPINFO *bi = (BITMAPINFO *)dib;
    BYTE *BitData = (BYTE *)bi + bi->bmiHeader.biSize + sizeof(RGBQUAD) * bi->bmiHeader.biClrUsed;

    m_Properties.m_Data = BitData;
    *mp = (CKMovieProperties *)&m_Properties;

    return CK_OK;
}
