/**
 * WavReader.cpp - dr_wav-based WAV file reader for Virtools
 *
 * This implementation uses dr_wav (public domain, single-header library) to decode
 * WAV files. It supports PCM passthrough for already-PCM files and decodes non-PCM
 * formats (float, ADPCM, etc.) to PCM16.
 *
 * Design:
 *   - PCM passthrough (original format preserved for PCM files)
 *   - Legacy Seek(): Source file byte offset interpretation
 *   - Chunk sizing: ~125ms worth of frames per decode operation
 */
#include "WavReader.h"

// Define DR_WAV_IMPLEMENTATION before including dr_wav.h
// This should only be done in ONE translation unit
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

// PCM format tag (WAVE_FORMAT_PCM) without pulling in Windows multimedia headers.
static const CKWORD WAV_FORMAT_TAG_PCM = 0x0001;

// Default chunk duration in milliseconds
static const unsigned int CHUNK_DURATION_MS = 125;

// Minimum buffer size in bytes
static const unsigned int MIN_BUFFER_SIZE = 4096;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

WAVReader::WAVReader()
    : m_pWav(NULL), m_MemoryCopy(NULL), m_MemorySize(0), m_IsPcmPassthrough(0), m_OriginalBitsPerSample(0), m_InputFormatTag(0), m_InputBitsPerSample(0), m_InputBytesPerFrame(0), m_TotalFrames(0), m_TotalOutputBytes(0), m_CurrentFrame(0), m_DecodeBuffer(NULL), m_DecodeBufferSize(0), m_DecodedBytes(0), m_ChunkFrames(0)
{
    memset(&m_OutputFormat, 0, sizeof(m_OutputFormat));
    m_SubFileMem = NULL;
}

WAVReader::~WAVReader()
{
    Cleanup();
}

void WAVReader::Cleanup()
{
    // Match legacy behavior: CKSoundReader::m_SubFileMem may be set by the engine
    // to keep BigFile-streamed content alive for the lifetime of the reader.
    delete[] (CKBYTE *)m_SubFileMem;
    m_SubFileMem = NULL;

    if (m_pWav)
    {
        drwav *wav = (drwav *)m_pWav;
        drwav_uninit(wav);
        delete wav;
        m_pWav = NULL;
    }

    delete[] m_DecodeBuffer;
    m_DecodeBuffer = NULL;
    m_DecodeBufferSize = 0;

    delete[] m_MemoryCopy;
    m_MemoryCopy = NULL;
    m_MemorySize = 0;

    m_TotalFrames = 0;
    m_TotalOutputBytes = 0;
    m_CurrentFrame = 0;
    m_DecodedBytes = 0;
    m_ChunkFrames = 0;
    m_IsPcmPassthrough = 0;
    m_OriginalBitsPerSample = 0;
    m_InputFormatTag = 0;
    m_InputBitsPerSample = 0;
    m_InputBytesPerFrame = 0;

    memset(&m_OutputFormat, 0, sizeof(m_OutputFormat));
}

//////////////////////////////////////////////////////////////////////
// File/Memory Loading
//////////////////////////////////////////////////////////////////////

CKERROR WAVReader::OpenFile(char *file)
{
    if (!file || strlen(file) == 0)
        return CKSOUND_READER_GENERICERR;

    // Clean up any previous state
    Cleanup();

    // Allocate dr_wav structure
    drwav *wav = new drwav;
    if (!wav)
        return CKSOUND_READER_GENERICERR;

    // Initialize dr_wav from file
    if (!drwav_init_file(wav, file, NULL))
    {
        delete wav;
        return CKSOUND_READER_GENERICERR;
    }

    m_pWav = wav;

    return InitializeDecoder();
}

CKERROR WAVReader::ReadMemory(void *memory, int size)
{
    if (!memory || size <= 0)
        return CKSOUND_READER_GENERICERR;

    // Clean up any previous state
    Cleanup();

    // dr_wav_init_memory doesn't copy the data, so we need to keep it alive
    // Make a copy of the memory to ensure it stays valid
    m_MemoryCopy = new CKBYTE[size];
    if (!m_MemoryCopy)
        return CKSOUND_READER_GENERICERR;

    memcpy(m_MemoryCopy, memory, size);
    m_MemorySize = size;

    // Allocate dr_wav structure
    drwav *wav = new drwav;
    if (!wav)
    {
        delete[] m_MemoryCopy;
        m_MemoryCopy = NULL;
        return CKSOUND_READER_GENERICERR;
    }

    // Initialize dr_wav from memory
    if (!drwav_init_memory(wav, m_MemoryCopy, m_MemorySize, NULL))
    {
        delete wav;
        delete[] m_MemoryCopy;
        m_MemoryCopy = NULL;
        return CKSOUND_READER_GENERICERR;
    }

    m_pWav = wav;

    return InitializeDecoder();
}

//////////////////////////////////////////////////////////////////////
// Internal Initialization
//////////////////////////////////////////////////////////////////////

CKERROR WAVReader::InitializeDecoder()
{
    if (!m_pWav)
        return CKSOUND_READER_GENERICERR;

    drwav *wav = (drwav *)m_pWav;

    // Store total frames.
    // Some WAVs (notably with placeholder chunk sizes) can report 0 here; in that case
    // we do a one-time scan to compute the total so GetDataSize()/streaming works.
    drwav_uint64 totalFrames64 = wav->totalPCMFrameCount;
    if (totalFrames64 == 0)
    {
        // Best-effort: scan the whole stream and count frames.
        // This is only done for edge-case files where the total isn't known up front.
        if (drwav_seek_to_pcm_frame(wav, 0))
        {
            const drwav_uint64 scanChunkFrames = 4096;
            drwav_int16 *tmp = new drwav_int16[(size_t)scanChunkFrames * (size_t)wav->channels];
            if (tmp)
            {
                drwav_uint64 scanned = 0;
                for (;;)
                {
                    const drwav_uint64 got = drwav_read_pcm_frames_s16(wav, scanChunkFrames, tmp);
                    if (got == 0)
                        break;
                    scanned += got;
                }
                totalFrames64 = scanned;
                delete[] tmp;
            }

            // Rewind for normal decoding.
            drwav_seek_to_pcm_frame(wav, 0);
        }
    }

    if (totalFrames64 > 0xFFFFFFFFu)
        m_TotalFrames = 0xFFFFFFFFu;
    else
        m_TotalFrames = (unsigned long)totalFrames64;
    m_CurrentFrame = 0;

    // Determine if we can use PCM passthrough
    // PCM passthrough is possible when the source is PCM format
    // with 8, 16, 24, or 32 bits per sample
    m_IsPcmPassthrough = 0;
    m_OriginalBitsPerSample = wav->bitsPerSample;

    // Track input format for legacy-ish Seek()
    m_InputFormatTag = wav->translatedFormatTag;
    m_InputBitsPerSample = wav->bitsPerSample;
    m_InputBytesPerFrame = 0;

    if (wav->translatedFormatTag == DR_WAVE_FORMAT_PCM)
    {
        // Check for supported PCM bit depths
        if (m_OriginalBitsPerSample == 8 ||
            m_OriginalBitsPerSample == 16 ||
            m_OriginalBitsPerSample == 24 ||
            m_OriginalBitsPerSample == 32)
        {
            m_IsPcmPassthrough = 1;
        }
    }

    // For uncompressed formats, we can map "source data bytes" to a PCM frame index.
    // dr_wav exposes translatedFormatTag, which is either PCM, IEEE float, or something compressed.
    if (wav->translatedFormatTag == DR_WAVE_FORMAT_PCM || wav->translatedFormatTag == DR_WAVE_FORMAT_IEEE_FLOAT)
    {
        const unsigned int bytesPerSample = (wav->bitsPerSample / 8);
        if (bytesPerSample != 0)
            m_InputBytesPerFrame = (unsigned int)(wav->channels * bytesPerSample);
    }

    // Set up output format
    m_OutputFormat.wFormatTag = WAV_FORMAT_TAG_PCM;
    m_OutputFormat.nChannels = (CKWORD)wav->channels;
    m_OutputFormat.nSamplesPerSec = (CKDWORD)wav->sampleRate;

    if (m_IsPcmPassthrough)
    {
        // Passthrough: use original bit depth
        m_OutputFormat.wBitsPerSample = (CKWORD)m_OriginalBitsPerSample;
    }
    else
    {
        // Non-PCM: decode to 16-bit
        m_OutputFormat.wBitsPerSample = 16;
    }

    m_OutputFormat.nBlockAlign = m_OutputFormat.nChannels * m_OutputFormat.wBitsPerSample / 8;
    m_OutputFormat.nAvgBytesPerSec = m_OutputFormat.nSamplesPerSec * m_OutputFormat.nBlockAlign;
    m_OutputFormat.cbSize = 0;

    // Calculate total output size
    m_TotalOutputBytes = m_TotalFrames * m_OutputFormat.nBlockAlign;

    // Calculate chunk size and allocate buffer
    m_ChunkFrames = CalculateChunkFrames();
    AllocateDecodeBuffer();

    if (!m_DecodeBuffer)
        return CKSOUND_READER_GENERICERR;

    return CK_OK;
}

unsigned int WAVReader::CalculateChunkFrames() const
{
    drwav *wav = (drwav *)m_pWav;
    if (!wav || wav->sampleRate == 0)
        return 0;

    // Calculate frames for ~125ms of audio
    // frames = sampleRate * (duration_ms / 1000)
    unsigned int frames = (wav->sampleRate * CHUNK_DURATION_MS) / 1000;

    // Ensure at least some minimum number of frames
    if (frames < 1024)
        frames = 1024;

    // Don't exceed total frames
    if (frames > m_TotalFrames)
        frames = (unsigned int)m_TotalFrames;

    return frames;
}

void WAVReader::AllocateDecodeBuffer()
{
    // Calculate buffer size based on chunk frames and output format
    unsigned int bufferSize = m_ChunkFrames * m_OutputFormat.nBlockAlign;

    // Ensure minimum buffer size
    if (bufferSize < MIN_BUFFER_SIZE)
        bufferSize = MIN_BUFFER_SIZE;

    // Allocate buffer
    m_DecodeBuffer = new CKBYTE[bufferSize];
    m_DecodeBufferSize = m_DecodeBuffer ? bufferSize : 0;
    m_DecodedBytes = 0;
}

//////////////////////////////////////////////////////////////////////
// Streaming Decode API
//////////////////////////////////////////////////////////////////////

CKERROR WAVReader::Decode()
{
    if (!m_pWav || !m_DecodeBuffer)
        return CKSOUND_READER_GENERICERR;

    drwav *wav = (drwav *)m_pWav;

    // Check if we've reached the end
    if (m_CurrentFrame >= m_TotalFrames)
    {
        m_DecodedBytes = 0;
        return CKSOUND_READER_EOF;
    }

    // Calculate how many frames to read this chunk
    unsigned int framesToRead = m_ChunkFrames;
    unsigned long remainingFrames = m_TotalFrames - m_CurrentFrame;
    if (framesToRead > remainingFrames)
        framesToRead = (unsigned int)remainingFrames;

    drwav_uint64 framesRead = 0;

    if (m_IsPcmPassthrough)
    {
        // PCM passthrough: read raw PCM frames
        framesRead = drwav_read_pcm_frames(wav, framesToRead, m_DecodeBuffer);
    }
    else
    {
        // Non-PCM: decode to 16-bit signed PCM
        framesRead = drwav_read_pcm_frames_s16(wav, framesToRead, (drwav_int16 *)m_DecodeBuffer);
    }

    if (framesRead == 0)
    {
        m_DecodedBytes = 0;
        return CKSOUND_READER_EOF;
    }

    // Update state
    m_CurrentFrame += (unsigned long)framesRead;
    m_DecodedBytes = (unsigned int)(framesRead * m_OutputFormat.nBlockAlign);

    return CK_OK;
}

CKERROR WAVReader::GetDataBuffer(CKBYTE **buf, int *size)
{
    if (!buf || !size)
        return CKSOUND_READER_GENERICERR;

    *buf = m_DecodeBuffer;
    *size = (int)m_DecodedBytes;

    return CK_OK;
}

//////////////////////////////////////////////////////////////////////
// Format & Size Info
//////////////////////////////////////////////////////////////////////

CKERROR WAVReader::GetWaveFormat(CKWaveFormat *wfe)
{
    if (!wfe)
        return CKSOUND_READER_GENERICERR;

    memcpy(wfe, &m_OutputFormat, sizeof(CKWaveFormat));
    return CK_OK;
}

int WAVReader::GetDataSize()
{
    return (int)m_TotalOutputBytes;
}

int WAVReader::GetDuration()
{
    if (m_OutputFormat.nAvgBytesPerSec == 0)
        return 0;

    // Duration in milliseconds
    return (int)(((float)m_TotalOutputBytes * 1000.0f) / (float)m_OutputFormat.nAvgBytesPerSec);
}

//////////////////////////////////////////////////////////////////////
// Playback Control
//////////////////////////////////////////////////////////////////////

CKERROR WAVReader::Seek(int pos)
{
    if (!m_pWav)
        return CKSOUND_READER_GENERICERR;

    drwav *wav = (drwav *)m_pWav;

    // Legacy-ish behavior: interpret pos as "source data bytes".
    // For PCM/IEEE-float this maps exactly to a frame index.
    // For compressed formats, dr_wav cannot seek by compressed-byte offset; we fall back
    // to interpreting pos as decoded PCM bytes.

    if (pos < 0)
        pos = 0;

    // Convert byte position to frame index
    unsigned long frameIndex = 0;
    unsigned int bytesPerFrame = 0;
    if (m_InputBytesPerFrame != 0)
        bytesPerFrame = m_InputBytesPerFrame;
    else
        bytesPerFrame = (unsigned int)m_OutputFormat.nBlockAlign;

    if (bytesPerFrame != 0)
        frameIndex = (unsigned long)pos / bytesPerFrame;

    // Clamp to valid range
    if (frameIndex > m_TotalFrames)
        frameIndex = m_TotalFrames;

    // Seek dr_wav to the target frame
    if (!drwav_seek_to_pcm_frame(wav, frameIndex))
    {
        return CKSOUND_READER_GENERICERR;
    }

    m_CurrentFrame = frameIndex;
    m_DecodedBytes = 0;

    return CK_OK;
}

CKERROR WAVReader::Play()
{
    // Playback control is handled by higher-level sound objects
    return CK_OK;
}

CKERROR WAVReader::Stop()
{
    // Playback control is handled by higher-level sound objects
    return CK_OK;
}

//////////////////////////////////////////////////////////////////////
// Resource Management
//////////////////////////////////////////////////////////////////////

void WAVReader::Release()
{
    delete this;
}

//////////////////////////////////////////////////////////////////////
// Plugin registration
///////////////////////////////////////////////////////////////////////

#ifdef CK_LIB
#define RegisterBehaviorDeclarations Register_WavReader_BehaviorDeclarations
#define InitInstance _WavReader_InitInstance
#define ExitInstance _WavReader_ExitInstance
#define CKGetPluginInfoCount CKGet_WavReader_PluginInfoCount
#define CKGetPluginInfo CKGet_WavReader_PluginInfo
#define g_PluginInfo g_WavReader_PluginInfo
#define CKGetReader CKGet_WavReader_Reader
#else
#define RegisterBehaviorDeclarations RegisterBehaviorDeclarations
#define InitInstance InitInstance
#define ExitInstance ExitInstance
#define CKGetPluginInfoCount CKGetPluginInfoCount
#define CKGetPluginInfo CKGetPluginInfo
#define g_PluginInfo g_PluginInfo
#define CKGetReader CKGetReader
#endif

#define READER_COUNT 1
CKPluginInfo g_PluginInfo[READER_COUNT];

#define READER_VERSION 0x00000001
#define WAVREADER_GUID CKGUID(0x61abc44f, 0xe1233343)

CKPluginInfo *WAVReader::GetReaderInfo()
{
    return &g_PluginInfo[0];
}

PLUGIN_EXPORT CKDataReader *CKGetReader(int pos)
{
    return new WAVReader;
}

PLUGIN_EXPORT int CKGetPluginInfoCount()
{
    return READER_COUNT;
}

PLUGIN_EXPORT CKPluginInfo *CKGetPluginInfo(int index)
{
    g_PluginInfo[0].m_GUID = WAVREADER_GUID;
    g_PluginInfo[0].m_Version = READER_VERSION;
    g_PluginInfo[0].m_Description = "Wav Sound Files";
    g_PluginInfo[0].m_Summary = "Wav reader";
    g_PluginInfo[0].m_Extension = "Wav";
    g_PluginInfo[0].m_Author = "Virtools";
    g_PluginInfo[0].m_InitInstanceFct = NULL;
    g_PluginInfo[0].m_ExitInstanceFct = NULL;
    g_PluginInfo[0].m_Type = CKPLUGIN_SOUND_READER; // Plugin Type

    return &g_PluginInfo[0];
}
