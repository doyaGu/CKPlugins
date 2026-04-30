#include "AVIReader.h"

#include "VxMath.h"

#include <cstring>
#include <limits>

namespace
{

constexpr int kMaxVideoDimension = 16384;
constexpr size_t kMaxOutputFrameBytes = 256u * 1024u * 1024u; // 256 MiB

} // namespace

// ===========================================================================
// Plugin entry points (CK_LIB / DLL naming)
// ===========================================================================

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

PLUGIN_EXPORT CKDataReader *CKGetReader(int pos)
{
    return new AVIReader();
}

PLUGIN_EXPORT int CKGetPluginInfoCount()
{
    return READER_COUNT;
}

PLUGIN_EXPORT CKPluginInfo *CKGetPluginInfo(int index)
{
    g_PluginInfo.m_Author = "Virtools";
    g_PluginInfo.m_Description = "AVI Movie Reader";
    g_PluginInfo.m_Extension = "Avi";
    g_PluginInfo.m_Type = CKPLUGIN_MOVIE_READER;
    g_PluginInfo.m_Version = AVI_READER_VERSION;
    g_PluginInfo.m_InitInstanceFct = nullptr;
    g_PluginInfo.m_GUID = AVI_READER_GUID;
    g_PluginInfo.m_Summary = "AVI Reader";
    return &g_PluginInfo;
}

// ===========================================================================
// Construction / destruction
// ===========================================================================

AVIReader::AVIReader()
    : m_VideoStream(-1),
      m_FrameCount(0),
      m_OutputStride(0),
      m_LastDecodedFrame(-1)
{
    m_Properties.m_Ext = "avi";
    m_Properties.m_ReaderGuid = AVI_READER_GUID;
    m_Properties.m_Data = nullptr;
}

AVIReader::~AVIReader()
{
    ReleaseAll();
}

CKPluginInfo *AVIReader::GetReaderInfo()
{
    return &g_PluginInfo;
}

void AVIReader::ReleaseAll()
{
    m_Decoder.reset();
    m_Demuxer.Close();
    m_VideoStream = -1;
    m_FrameCount = 0;
    m_OutputStride = 0;
    m_FrameBuffer.clear();
    m_CompressedBuf.clear();
    m_LastDecodedFrame = -1;
    m_Properties.m_Data = nullptr;
}

// ===========================================================================
// Query methods
// ===========================================================================

int AVIReader::GetMovieFrameCount()
{
    return m_FrameCount;
}

int AVIReader::GetMovieLength()
{
    if (m_VideoStream < 0)
        return 0;
    return m_Demuxer.GetDurationMs(m_VideoStream);
}

bool AVIReader::DecodeFramePayload(int frameIndex)
{
    const avi::AviStreamInfo *info = m_Demuxer.GetStreamInfo(m_VideoStream);
    if (!info)
        return false;

    if (!m_Demuxer.ReadFrameData(m_VideoStream, frameIndex, m_CompressedBuf))
        return false;

    return m_Decoder->Decode(m_CompressedBuf.data(), m_CompressedBuf.size(),
                             info->width, info->height,
                             m_FrameBuffer.data(), m_OutputStride);
}

bool AVIReader::DecodeFrameWithDependencies(int frameIndex)
{
    if (!m_Decoder->NeedsSequentialFrames())
    {
        if (!DecodeFramePayload(frameIndex))
            return false;
        m_LastDecodedFrame = frameIndex;
        return true;
    }

    if (m_LastDecodedFrame == frameIndex)
        return true;

    int startFrame = 0;
    if (m_LastDecodedFrame >= 0 && frameIndex > m_LastDecodedFrame)
    {
        // When already decoded up to N, decoding N+1..target preserves correctness.
        startFrame = m_LastDecodedFrame + 1;
    }
    else
    {
        // Strict seek correctness for delta codecs: do not trust index keyframe flags.
        // Re-decode from frame 0 whenever seeking backwards or to unrelated positions.
        m_Decoder->Reset();
        m_LastDecodedFrame = -1;
        startFrame = 0;
    }

    for (int i = startFrame; i <= frameIndex; ++i)
    {
        if (!DecodeFramePayload(i))
        {
            m_LastDecodedFrame = -1;
            return false;
        }
        m_LastDecodedFrame = i;
    }

    return true;
}

// ===========================================================================
// OpenFile
// ===========================================================================

CKERROR AVIReader::OpenFile(CKSTRING name)
{
    if (!name || !name[0])
        return CKMOVIEERROR_READERROR;

    ReleaseAll();

    // --- 1. Open and parse the AVI container ---
    if (!m_Demuxer.Open(name))
        return CKMOVIEERROR_UNSUPPORTEDFILE;

    // --- 2. Find the first video stream ---
    m_VideoStream = m_Demuxer.FindFirstVideoStream();
    if (m_VideoStream < 0)
    {
        ReleaseAll();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }

    const avi::AviStreamInfo *info = m_Demuxer.GetStreamInfo(m_VideoStream);
    if (!info || info->width <= 0 || info->height <= 0)
    {
        ReleaseAll();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }
    if (info->width > kMaxVideoDimension || info->height > kMaxVideoDimension)
    {
        ReleaseAll();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }

    // --- 3. Create the appropriate frame decoder ---
    m_Decoder = CreateFrameDecoder(*info);
    if (!m_Decoder)
    {
        ReleaseAll();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }

    // --- 4. Set up output format (always 32bpp ARGB, bottom-up) ---
    m_FrameCount = m_Demuxer.GetFrameCount(m_VideoStream);
    if (m_FrameCount <= 0)
    {
        ReleaseAll();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }

    const int width = info->width;
    const int height = info->height;
    if (width > INT_MAX / 4)
    {
        ReleaseAll();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }
    m_OutputStride = width * 4; // 32bpp, no padding needed
    if (height > INT_MAX / m_OutputStride)
    {
        ReleaseAll();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }
    if (static_cast<size_t>(height) > (SIZE_MAX / static_cast<size_t>(m_OutputStride)))
    {
        ReleaseAll();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }
    const size_t frameBytes = static_cast<size_t>(m_OutputStride) * static_cast<size_t>(height);
    if (frameBytes == 0 || frameBytes > kMaxOutputFrameBytes)
    {
        ReleaseAll();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }

    m_Properties.m_Format.Width = width;
    m_Properties.m_Format.Height = height;
    m_Properties.m_Format.BitsPerPixel = 32;
    m_Properties.m_Format.BytesPerLine = m_OutputStride;
    m_Properties.m_Format.RedMask = R_MASK;
    m_Properties.m_Format.GreenMask = G_MASK;
    m_Properties.m_Format.BlueMask = B_MASK;
    m_Properties.m_Format.AlphaMask = A_MASK;

    // Pre-allocate the frame buffer.
    try
    {
        m_FrameBuffer.resize(frameBytes);
    }
    catch (...)
    {
        ReleaseAll();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }

    // --- 5. Verify we can decode the first frame ---
    m_Decoder->Reset();
    if (!DecodeFrameWithDependencies(0))
    {
        ReleaseAll();
        return CKMOVIEERROR_UNSUPPORTEDFILE;
    }

    m_Properties.m_Data = m_FrameBuffer.data();
    return CK_OK;
}

// ===========================================================================
// ReadFrame
// ===========================================================================

CKERROR AVIReader::ReadFrame(int f, CKMovieProperties **mp)
{
    if (!mp || m_VideoStream < 0 || !m_Decoder)
        return CKMOVIEERROR_GENERIC;
    *mp = nullptr;

    if (f < 0 || f >= m_FrameCount)
        return CKMOVIEERROR_GENERIC;

    if (!DecodeFrameWithDependencies(f))
    {
        return CKMOVIEERROR_READERROR;
    }

    m_Properties.m_Data = m_FrameBuffer.data();
    *mp = &m_Properties;
    return CK_OK;
}
