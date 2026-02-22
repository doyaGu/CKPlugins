#include "CKMovieReader.h"

#include "AviDemuxer.h"
#include "FrameDecoder.h"

#include <memory>
#include <vector>

#define AVI_READER_VERSION 0x00000001
#define AVI_READER_GUID CKGUID(0x67541bfe, 0x75e510c0)

struct AVIMovieProperties : public CKMovieProperties
{
    AVIMovieProperties()
    {
        m_Size = sizeof(AVIMovieProperties);
    }
};

// ---------------------------------------------------------------------------
// AVIReader -- dependency-free AVI movie reader.
// Uses an internal RIFF/AVI demuxer (classic + OpenDML)
// and frame decoder (BI_RGB / MJPEG) with no VFW dependency.
// ---------------------------------------------------------------------------
class AVIReader : public CKMovieReader
{
public:
    AVIReader();
    ~AVIReader();

    void Release() override { delete this; }

    CKPluginInfo *GetReaderInfo() override;

    int GetOptionsCount() override { return 0; }
    CKSTRING GetOptionDescription(int i) override { return nullptr; }

    CK_DATAREADER_FLAGS GetFlags() override { return CK_DATAREADER_FILELOAD; }

    int GetMovieFrameCount() override;
    int GetMovieLength() override;

    CKERROR OpenFile(CKSTRING name) override;
    CKERROR ReadFrame(int f, CKMovieProperties **mp) override;

    CKERROR OpenMemory(CKSTRING name) override { return CKERR_NOTIMPLEMENTED; }
    CKERROR OpenAsynchronousFile(CKSTRING name) override { return CKERR_NOTIMPLEMENTED; }

protected:
    void ReleaseAll();
    bool DecodeFramePayload(int frameIndex);
    bool DecodeFrameWithDependencies(int frameIndex);

    AVIMovieProperties             m_Properties;
    AviDemuxer                     m_Demuxer;
    std::unique_ptr<IFrameDecoder> m_Decoder;
    int                            m_VideoStream;    ///< Index of selected video stream.
    int                            m_FrameCount;

    // Owned frame buffer: 32bpp ARGB, bottom-up layout.
    std::vector<uint8_t>           m_FrameBuffer;
    int                            m_OutputStride;

    // Temporary buffer for compressed frame data.
    std::vector<uint8_t>           m_CompressedBuf;
    int                            m_LastDecodedFrame;
};
