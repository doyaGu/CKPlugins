#include "AVIReader.h"

#include "VxMath.h"

#include <string.h>
#include <limits.h>

static int ComputeStrideBytes(LONG width, WORD bitsPerPixel)
{
    if (width <= 0 || bitsPerPixel == 0)
        return 0;

    const unsigned long long bitsPerLine = static_cast<unsigned long long>(width) * static_cast<unsigned long long>(bitsPerPixel);
    const unsigned long long stride = ((bitsPerLine + 31ULL) & ~31ULL) / 8ULL;
    if (stride == 0 || stride > static_cast<unsigned long long>(INT_MAX))
        return 0;

    return static_cast<int>(stride);
}

static BYTE *GetDibBits(BITMAPINFO *bi)
{
    if (!bi)
        return NULL;

    const BITMAPINFOHEADER &header = bi->bmiHeader;
    if (header.biSize < sizeof(BITMAPINFOHEADER))
        return NULL;

    BYTE *bits = reinterpret_cast<BYTE *>(bi) + header.biSize;
    const bool hasLegacyInfoHeader = (header.biSize == sizeof(BITMAPINFOHEADER));
    if (hasLegacyInfoHeader && header.biCompression == BI_BITFIELDS)
    {
        bits += 3 * sizeof(DWORD);
    }
#ifdef BI_ALPHABITFIELDS
    else if (hasLegacyInfoHeader && header.biCompression == BI_ALPHABITFIELDS)
    {
        bits += 4 * sizeof(DWORD);
    }
#endif
    else
    {
        DWORD colorCount = header.biClrUsed;
        if (colorCount == 0 && header.biBitCount <= 8)
            colorCount = 1u << header.biBitCount;

        bits += colorCount * sizeof(RGBQUAD);
    }

    return bits;
}

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

#define READER_COUNT 1
CKPluginInfo g_PluginInfo;

/**********************************************
 Called by the engine when a file with the AVI
 extension is being loaded, a reader has to be
 created.
***********************************************/
PLUGIN_EXPORT CKDataReader *CKGetReader(int pos)
{
    return new AVIReader();
}

PLUGIN_EXPORT int CKGetPluginInfoCount()
{
    return READER_COUNT;
}

/**********************************************
Called by the engine when it parses for available
plugins. Returns the information about this plugin.
The more important being the extension for the
movie files it is able to load (in this case "Avi".
***********************************************/
PLUGIN_EXPORT CKPluginInfo *CKGetPluginInfo(int index)
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

    m_FrameStride = 0;
    m_TopDown = false;
    m_ExpandTo32 = false;
    m_OutputStride = 0;
    m_TopDownBuffer = NULL;
    m_TopDownBufferSize = 0;

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
    if (m_TopDownBuffer)
        delete[] m_TopDownBuffer;
    m_TopDownBuffer = NULL;
    m_TopDownBufferSize = 0;

    if (m_Frame)
        AVIStreamGetFrameClose(m_Frame);
    m_Frame = NULL;

    if (m_Stream)
        AVIStreamRelease(m_Stream);
    m_Stream = NULL;

    m_FrameCount = 0;
    m_FrameStride = 0;
    m_TopDown = false;
    m_ExpandTo32 = false;
    m_OutputStride = 0;

    m_Properties.m_Data = NULL;
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
    if (!m_Stream)
        return 0;

    return AVIStreamSampleToTime(m_Stream, m_FrameCount);
}

/*********************************************************
Open a .AVI file and retrieve information : Number of
frames and pixel format of the movie.
*********************************************************/
CKERROR AVIReader::OpenFile(CKSTRING name)
{
    if (!name || !name[0])
        return CKMOVIEERROR_READERROR;

    ReleaseAVI();

    // Try to create a AVIStream from the file , if failed return an error
    HRESULT hr = 0;
#if defined(UNICODE) || defined(_UNICODE)
    WCHAR wName[MAX_PATH];
    wName[0] = L'\0';
    const int conv = MultiByteToWideChar(CP_ACP, 0, name, -1, wName, MAX_PATH);
    if (conv <= 0)
        return CKMOVIEERROR_READERROR;
    hr = AVIStreamOpenFromFileW(&m_Stream, wName, streamtypeVIDEO, 0, OF_READ, NULL);
#else
    hr = AVIStreamOpenFromFileA(&m_Stream, name, streamtypeVIDEO, 0, OF_READ, NULL);
#endif
    if (hr)
        return CKMOVIEERROR_UNSUPPORTEDFILE;

    // Try to open frames. Prefer 32bpp output to match engine texture format and
    // avoid potential 16bpp conversion issues during movie blits.
    BITMAPINFOHEADER wanted;
    memset(&wanted, 0, sizeof(wanted));
    wanted.biSize = sizeof(BITMAPINFOHEADER);
    wanted.biPlanes = 1;
    wanted.biBitCount = 32;
    wanted.biCompression = BI_RGB;
    m_Frame = AVIStreamGetFrameOpen(m_Stream, &wanted);
    if (!m_Frame)
        m_Frame = AVIStreamGetFrameOpen(m_Stream, NULL);
    if (!m_Frame)
        m_Frame = AVIStreamGetFrameOpen(m_Stream, (BITMAPINFOHEADER *)AVIGETFRAMEF_BESTDISPLAYFMT);
    if (!m_Frame)
    {
        ReleaseAVI();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }

    // Try to get the first frame of the movie
    const LONG streamStart = AVIStreamStart(m_Stream);
    if (streamStart < 0)
    {
        ReleaseAVI();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }

    if (void *dib = AVIStreamGetFrame(m_Frame, streamStart))
    {
        BITMAPINFO *bi = (BITMAPINFO *)dib;
        const BITMAPINFOHEADER &header = bi->bmiHeader;
        BYTE *BitData = GetDibBits(bi);

        if (!BitData || header.biWidth <= 0 || header.biHeight == 0 || header.biBitCount <= 0 || header.biHeight == LONG_MIN)
        {
            ReleaseAVI();
            return CKMOVIEERROR_UNSUPPORTEDFILE;
        }

        const int absHeight = (header.biHeight < 0) ? static_cast<int>(-header.biHeight) : static_cast<int>(header.biHeight);
        int bytesPerLine = 0;
        if (header.biSizeImage > 0 && absHeight > 0)
        {
            const unsigned long long strideFromImage = static_cast<unsigned long long>(header.biSizeImage) / static_cast<unsigned long long>(absHeight);
            if (strideFromImage > 0 && strideFromImage <= static_cast<unsigned long long>(INT_MAX))
                bytesPerLine = static_cast<int>(strideFromImage);
        }
        if (bytesPerLine <= 0)
            bytesPerLine = ComputeStrideBytes(header.biWidth, header.biBitCount);
        if (bytesPerLine <= 0)
        {
            ReleaseAVI();
            return CKMOVIEERROR_UNSUPPORTEDFILE;
        }

        // Decode output settings
        m_TopDown = (header.biHeight < 0);
        m_FrameStride = bytesPerLine;

        m_ExpandTo32 = (header.biBitCount == 16);
        if (m_ExpandTo32)
        {
            m_OutputStride = header.biWidth * 4;
            m_Properties.m_Format.Width = header.biWidth;
            m_Properties.m_Format.Height = absHeight;
            m_Properties.m_Format.BitsPerPixel = 32;
            m_Properties.m_Format.BytesPerLine = m_OutputStride;
            m_Properties.m_Format.RedMask = R_MASK;
            m_Properties.m_Format.GreenMask = G_MASK;
            m_Properties.m_Format.BlueMask = B_MASK;
            m_Properties.m_Format.AlphaMask = A_MASK;
        }
        else
        {
            m_OutputStride = bytesPerLine;
            m_Properties.m_Format.Width = header.biWidth;
            m_Properties.m_Format.Height = absHeight;
            m_Properties.m_Format.BitsPerPixel = (char)header.biBitCount;
            m_Properties.m_Format.BytesPerLine = bytesPerLine;
            VxBppToMask(m_Properties.m_Format);
            m_Properties.m_Format.AlphaMask = 0;
        }

        // Get the number of frame in the movie
        m_FrameCount = AVIStreamLength(m_Stream);
        if (m_FrameCount <= 0)
        {
            ReleaseAVI();
            return CKMOVIEERROR_UNSUPPORTEDFILE;
        }
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
    if (!mp || !m_Frame || !m_Stream)
        return CKMOVIEERROR_GENERIC;
    *mp = NULL;

    if ((DWORD)f >= (DWORD)m_FrameCount)
        return CKMOVIEERROR_GENERIC;

    const LONG streamStart = AVIStreamStart(m_Stream);
    void *dib = AVIStreamGetFrame(m_Frame, f + streamStart);
    if (!dib)
        return CKMOVIEERROR_READERROR;

    BITMAPINFO *bi = (BITMAPINFO *)dib;
    const BITMAPINFOHEADER &header = bi->bmiHeader;
    BYTE *BitData = GetDibBits(bi);
    if (!BitData)
        return CKMOVIEERROR_READERROR;

    const bool frameTopDown = (header.biHeight < 0);
    const int absHeight = (header.biHeight < 0) ? static_cast<int>(-header.biHeight) : static_cast<int>(header.biHeight);
    if (header.biWidth != m_Properties.m_Format.Width ||
        absHeight != m_Properties.m_Format.Height ||
        header.biBitCount != (m_ExpandTo32 ? 16 : (WORD)m_Properties.m_Format.BitsPerPixel))
    {
        return CKMOVIEERROR_READERROR;
    }

    int strideNow = 0;
    if (header.biSizeImage > 0 && absHeight > 0)
    {
        const unsigned long long strideFromImage = static_cast<unsigned long long>(header.biSizeImage) / static_cast<unsigned long long>(absHeight);
        if (strideFromImage > 0 && strideFromImage <= static_cast<unsigned long long>(INT_MAX))
            strideNow = static_cast<int>(strideFromImage);
    }
    if (strideNow <= 0)
        strideNow = ComputeStrideBytes(header.biWidth, header.biBitCount);
    if (strideNow <= 0)
        strideNow = m_FrameStride;
    if (strideNow <= 0)
        return CKMOVIEERROR_READERROR;

    const int height = m_Properties.m_Format.Height;
    const int srcStride = strideNow;
    const int requiredSrc = (height > 0 && srcStride > 0) ? (height * srcStride) : 0;
    if (requiredSrc <= 0)
        return CKMOVIEERROR_READERROR;

    if (header.biSizeImage > 0 && header.biSizeImage < (DWORD)requiredSrc)
        return CKMOVIEERROR_READERROR;

    if (m_ExpandTo32)
    {
        const int dstStride = m_OutputStride;
        const int requiredDst = height * dstStride;
        if (!m_TopDownBuffer || m_TopDownBufferSize < requiredDst)
        {
            delete[] m_TopDownBuffer;
            m_TopDownBuffer = new BYTE[requiredDst];
            m_TopDownBufferSize = requiredDst;
        }

        // Convert into bottom-up 32bpp ARGB buffer (first row is bottom row).
        for (int y = 0; y < height; ++y)
        {
            const int srcIndex = frameTopDown ? (height - 1 - y) : y;
            const BYTE *srcRow = BitData + srcIndex * srcStride;
            XDWORD *dstRow = (XDWORD *)(m_TopDownBuffer + y * dstStride);

            const WORD *src16 = (const WORD *)srcRow;
            for (int x = 0; x < m_Properties.m_Format.Width; ++x)
            {
                const WORD p = src16[x];
                // Assume 16bpp BI_RGB is 5-5-5 (Windows default).
                const XDWORD r5 = (p >> 10) & 0x1F;
                const XDWORD g5 = (p >> 5) & 0x1F;
                const XDWORD b5 = (p)&0x1F;
                const XDWORD r = (r5 * 255u) / 31u;
                const XDWORD g = (g5 * 255u) / 31u;
                const XDWORD b = (b5 * 255u) / 31u;
                dstRow[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }

        BitData = m_TopDownBuffer;
    }
    else
    {
        // Always copy into an owned buffer (bottom-up layout) to avoid exposing VFW internal pointers.
        const int dstStride = m_OutputStride;
        const int requiredDst = height * dstStride;
        if (!m_TopDownBuffer || m_TopDownBufferSize < requiredDst)
        {
            delete[] m_TopDownBuffer;
            m_TopDownBuffer = new BYTE[requiredDst];
            m_TopDownBufferSize = requiredDst;
        }

        const int copyBytes = (srcStride < dstStride) ? srcStride : dstStride;
        if (copyBytes <= 0)
            return CKMOVIEERROR_READERROR;

        if (frameTopDown)
        {
            // Output buffer is bottom-up: first row in memory is bottom row.
            for (int y = 0; y < height; ++y)
            {
                const BYTE *srcRow = BitData + (height - 1 - y) * srcStride;
                BYTE *dstRow = m_TopDownBuffer + y * dstStride;
                memcpy(dstRow, srcRow, (size_t)copyBytes);
                if (dstStride > copyBytes)
                    memset(dstRow + copyBytes, 0, (size_t)(dstStride - copyBytes));
            }
        }
        else
        {
            for (int y = 0; y < height; ++y)
            {
                const BYTE *srcRow = BitData + y * srcStride;
                BYTE *dstRow = m_TopDownBuffer + y * dstStride;
                memcpy(dstRow, srcRow, (size_t)copyBytes);
                if (dstStride > copyBytes)
                    memset(dstRow + copyBytes, 0, (size_t)(dstStride - copyBytes));
            }
        }

        BitData = m_TopDownBuffer;
    }

    m_Properties.m_Data = BitData;
    *mp = (CKMovieProperties *)&m_Properties;
    return CK_OK;
}
