#ifndef WAVREADER_H
#define WAVREADER_H

#include "CKSoundReader.h"

// dr_wav is kept as an implementation detail in WavReader.cpp.
// We store it behind a void* here to avoid pulling the single-header library
// into all compilation units (and to avoid forward-decl type conflicts).

/**
 * WAVReader - A CKSoundReader implementation using dr_wav for WAV file decoding.
 *
 * Design principles:
 *   - PCM passthrough: Already-PCM files are passed through with their
 *     original bit depth; non-PCM formats (float, ADPCM, etc.) are decoded to PCM16.
 *   - Legacy Seek(): Position is interpreted as source file byte offset for
 *     backwards compatibility.
 *   - Chunk sizing: Uses ~125ms worth of frames per decode chunk, aligned to sample rate.
 *
 * Supported input formats (via dr_wav):
 *   - PCM (8/16/24/32-bit)
 *   - IEEE float (32/64-bit)
 *   - A-law, µ-law
 *   - Microsoft ADPCM, IMA ADPCM
 *
 * Output format:
 *   - PCM files: Original format (passthrough)
 *   - Non-PCM files: PCM 16-bit, original channels/sample rate
 */
class WAVReader : public CKSoundReader
{
public:
    WAVReader();
    virtual ~WAVReader();

    virtual void Release();
    virtual CKPluginInfo *GetReaderInfo();

    // Options (none supported)
    virtual int GetOptionsCount() { return 0; }
    virtual CKSTRING GetOptionDescription(int i) { return NULL; }

    // Capability flags
    virtual CK_DATAREADER_FLAGS GetFlags()
    {
        return (CK_DATAREADER_FLAGS)(CK_DATAREADER_FILELOAD | CK_DATAREADER_MEMORYLOAD);
    }

    // --- File/Memory Loading ---
    virtual CKERROR OpenFile(char *file);
    virtual CKERROR ReadMemory(void *memory, int size);

    // --- Streaming Decode API ---
    // Decodes next chunk of data; use GetDataBuffer() to retrieve decoded data
    virtual CKERROR Decode();
    // Gets the last decoded buffer and its size
    virtual CKERROR GetDataBuffer(CKBYTE **buf, int *size);

    // --- Format & Size Info ---
    // Gets the wave format of decoded data (output format)
    virtual CKERROR GetWaveFormat(CKWaveFormat *wfe);
    // Gets total decoded data size in bytes
    virtual int GetDataSize();
    // Gets play time length in milliseconds
    virtual int GetDuration();

    // --- Playback Control ---
    // Seek to position (legacy: interpreted as source file byte offset)
    virtual CKERROR Seek(int pos);
    virtual CKERROR Play();
    virtual CKERROR Stop();
    virtual CKERROR Pause() { return CK_OK; }
    virtual CKERROR Resume() { return CK_OK; }

private:
    // Internal initialization after dr_wav is opened
    CKERROR InitializeDecoder();
    // Allocate decode buffer based on format
    void AllocateDecodeBuffer();
    // Calculate chunk size in frames (~125ms)
    unsigned int CalculateChunkFrames() const;
    // Clean up all resources
    void Cleanup();

    // --- dr_wav state ---
    void *m_pWav; // dr_wav decoder instance (drwav*)

    // --- Memory source support ---
    CKBYTE *m_MemoryCopy; // Copy of memory data (for ReadMemory)
    int m_MemorySize;     // Size of memory copy

    // --- Format info ---
    CKWaveFormat m_OutputFormat;          // Output format exposed to engine
    CKBOOL m_IsPcmPassthrough;            // TRUE if we can pass through original PCM data
    unsigned int m_OriginalBitsPerSample; // Original bits per sample (for passthrough)

    // Input format info (used for legacy-ish Seek semantics).
    // For PCM/IEEE float, this is exact; for compressed formats, dr_wav can only seek by PCM frame.
    unsigned int m_InputFormatTag;
    unsigned int m_InputBitsPerSample;
    unsigned int m_InputBytesPerFrame;

    // --- Size tracking ---
    unsigned long m_TotalFrames;      // Total frames in the file
    unsigned long m_TotalOutputBytes; // Total decoded output size in bytes
    unsigned long m_CurrentFrame;     // Current frame position

    // --- Decode buffer ---
    CKBYTE *m_DecodeBuffer;          // Buffer for decoded chunk
    unsigned int m_DecodeBufferSize; // Size of decode buffer in bytes
    unsigned int m_DecodedBytes;     // Bytes decoded in last Decode() call
    unsigned int m_ChunkFrames;      // Frames per decode chunk (~125ms)
};

#endif // WAVREADER_H
