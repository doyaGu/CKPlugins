#ifndef AVIDEMUXER_H
#define AVIDEMUXER_H

#include "AviTypes.h"
#include "RiffReader.h"

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// AviDemuxer -- parses an AVI container (classic + OpenDML) and provides
// random-access to compressed frame payloads by stream and frame index.
//
// Responsibilities (Single Responsibility):
//   - Parse AVI headers (avih, strh, strf) to discover streams.
//   - Build a unified per-stream frame index from either classic idx1 or
//     OpenDML super-index/standard-index.
//   - Read compressed frame data on demand.
//
// Thread safety: not thread-safe (single-reader, single-thread).
// ---------------------------------------------------------------------------

class AviDemuxer
{
public:
    AviDemuxer();
    ~AviDemuxer();

    // Non-copyable
    AviDemuxer(const AviDemuxer &) = delete;
    AviDemuxer &operator=(const AviDemuxer &) = delete;

    /// Open an AVI file. Returns true on success.
    bool Open(const char *filename);

    /// Close the file and release all resources.
    void Close();

    /// True if a file is currently open and parsed.
    bool IsOpen() const { return m_Open; }

    // ------------------------------------------------------------------
    // Stream queries
    // ------------------------------------------------------------------

    /// Number of streams discovered in the file.
    int GetStreamCount() const { return static_cast<int>(m_Streams.size()); }

    /// Stream info for a given stream index. Returns nullptr if out of range.
    const avi::AviStreamInfo *GetStreamInfo(int streamIndex) const;

    /// Find the first video stream (returns -1 if none).
    int FindFirstVideoStream() const;

    /// Find the first audio stream (returns -1 if none).
    int FindFirstAudioStream() const;

    // ------------------------------------------------------------------
    // Frame queries
    // ------------------------------------------------------------------

    /// Number of indexed frames for a given stream.
    int GetFrameCount(int streamIndex) const;

    /// Duration in milliseconds for a given stream (derived from frame rate).
    int GetDurationMs(int streamIndex) const;

    /// Get the index entry for a specific frame. Returns nullptr if invalid.
    const avi::FrameIndexEntry *GetFrameInfo(int streamIndex, int frameIndex) const;

    // ------------------------------------------------------------------
    // Frame data access
    // ------------------------------------------------------------------

    /// Read the compressed payload for a specific frame into `outData`.
    /// Resizes `outData` to the frame's size. Returns true on success.
    bool ReadFrameData(int streamIndex, int frameIndex, std::vector<uint8_t> &outData);

private:
    // ------------------------------------------------------------------
    // Internal per-stream bookkeeping
    // ------------------------------------------------------------------
    struct StreamEntry
    {
        avi::AviStreamInfo info;
        std::vector<avi::FrameIndexEntry> index;
        std::vector<avi::FrameIndexEntry> headerIndex;

        // OpenDML: super-index data captured during header parse.
        // Empty if the file uses classic idx1 instead.
        std::vector<avi::AviSuperIndexEntry> superIndex;
        uint32_t superIndexChunkId; // dwChunkId from indx header

        StreamEntry() : superIndexChunkId(0) {}
    };

    // ------------------------------------------------------------------
    // Parsing helpers
    // ------------------------------------------------------------------

    /// Parse the top-level RIFF AVI header hierarchy.
    bool ParseRiff();

    /// Parse 'LIST hdrl' to discover streams.
    bool ParseHeaderList(const RiffReader::Chunk &hdrlChunk);

    /// Parse one 'LIST strl' block to populate a StreamEntry.
    bool ParseStreamList(const RiffReader::Chunk &strlChunk);

    /// Parse a 'strh' chunk into an AviStreamHeader.
    bool ParseStreamHeader(const RiffReader::Chunk &chunk, avi::AviStreamHeader &strh);

    /// Parse a video 'strf' chunk into a BitmapInfoHeader and extra format metadata.
    bool ParseVideoFormat(const RiffReader::Chunk &chunk, avi::BitmapInfoHeader &bih, StreamEntry &stream);

    /// Parse an audio 'strf' chunk into a WaveFormatHeader.
    bool ParseAudioFormat(const RiffReader::Chunk &chunk, avi::WaveFormatHeader &wfh);

    /// Parse an 'indx' chunk into the current stream entry (super-index or standard index).
    bool ParseStreamIndex(const RiffReader::Chunk &chunk, StreamEntry &stream);

    /// Parse an OpenDML super-index 'indx' chunk into the current stream entry.
    bool ParseSuperIndex(const RiffReader::Chunk &chunk, StreamEntry &stream);

    /// Build the frame index. Tries OpenDML first, falls back to idx1.
    bool BuildIndex();

    /// Build frame index from classic idx1 chunk.
    bool BuildIdx1Index();

    /// Build frame index from OpenDML super-index -> standard-index chain.
    bool BuildOpenDmlIndex();

    /// Parse one standard-index (ix##) chunk and append entries to `outEntries`.
    bool ParseStandardIndex(int64_t offset, uint32_t chunkSize, uint32_t expectedChunkId,
                            std::vector<avi::FrameIndexEntry> &outEntries);

    // ------------------------------------------------------------------
    // Data members
    // ------------------------------------------------------------------

    RiffReader               m_Reader;
    bool                     m_Open;
    avi::MainAviHeader       m_MainHeader;
    std::vector<StreamEntry> m_Streams;

    // Location of the first 'movi' list (used for idx1 offset base).
    int64_t m_MoviDataOffset; // absolute offset of the first byte after 'movi' form type
    int64_t m_MoviEndOffset;  // end of the movi list

    // Location of idx1 chunk (0 if absent).
    int64_t  m_Idx1Offset;
    uint32_t m_Idx1Size;
};

#endif // AVIDEMUXER_H
