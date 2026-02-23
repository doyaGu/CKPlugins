#ifndef AVIREADER_TESTUTILS_H
#define AVIREADER_TESTUTILS_H

// ===========================================================================
// TestUtils.h -- shared helpers for AVIReader unit tests.
//
// Provides:
//   - TempFile     RAII wrapper that writes bytes to a temp file and cleans up
//   - Binary builder helpers for constructing minimal RIFF / AVI buffers
// ===========================================================================

#include "AviTypes.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>

namespace testutil {

// ---------------------------------------------------------------------------
// Little-endian append helpers
// ---------------------------------------------------------------------------

inline void AppendU8(std::vector<uint8_t> &buf, uint8_t v)
{
    buf.push_back(v);
}

inline void AppendU16LE(std::vector<uint8_t> &buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

inline void AppendU32LE(std::vector<uint8_t> &buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>(v & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

inline void AppendU64LE(std::vector<uint8_t> &buf, uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFF));
}

inline void AppendI32LE(std::vector<uint8_t> &buf, int32_t v)
{
    AppendU32LE(buf, static_cast<uint32_t>(v));
}

inline void AppendBytes(std::vector<uint8_t> &buf, const void *data, size_t n)
{
    const auto *p = reinterpret_cast<const uint8_t *>(data);
    buf.insert(buf.end(), p, p + n);
}

inline void AppendZeros(std::vector<uint8_t> &buf, size_t n)
{
    buf.insert(buf.end(), n, 0);
}

// ---------------------------------------------------------------------------
// Patch a U32LE value at a specific offset in the buffer
// ---------------------------------------------------------------------------
inline void PatchU32LE(std::vector<uint8_t> &buf, size_t offset, uint32_t v)
{
    buf[offset + 0] = static_cast<uint8_t>(v & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    buf[offset + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    buf[offset + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

// ---------------------------------------------------------------------------
// TempFile -- RAII file that writes bytes to a temp path and deletes on dtor
// ---------------------------------------------------------------------------

class TempFile
{
public:
    explicit TempFile(const std::vector<uint8_t> &data, const std::string &suffix = ".bin")
    {
        namespace fs = std::filesystem;
        m_Path = (fs::temp_directory_path() / ("avireader_test_" +
                  std::to_string(reinterpret_cast<uintptr_t>(this)) + suffix)).string();

        FILE *f = nullptr;
#if defined(_MSC_VER) || defined(_WIN32)
        fopen_s(&f, m_Path.c_str(), "wb");
#else
        f = fopen(m_Path.c_str(), "wb");
#endif
        if (f)
        {
            fwrite(data.data(), 1, data.size(), f);
            fclose(f);
        }
    }

    ~TempFile()
    {
        std::filesystem::remove(m_Path);
    }

    TempFile(const TempFile &) = delete;
    TempFile &operator=(const TempFile &) = delete;

    const std::string &Path() const { return m_Path; }
    const char *CPath() const { return m_Path.c_str(); }

private:
    std::string m_Path;
};

// ---------------------------------------------------------------------------
// RIFF / AVI binary builders
// ---------------------------------------------------------------------------

/// Build a minimal RIFF file: RIFF header + formType + inner data.
inline std::vector<uint8_t> MakeRiffFile(uint32_t formType,
                                          const std::vector<uint8_t> &innerData)
{
    std::vector<uint8_t> buf;
    AppendU32LE(buf, avi::kFourCC_RIFF);
    AppendU32LE(buf, static_cast<uint32_t>(4 + innerData.size())); // size includes formType
    AppendU32LE(buf, formType);
    AppendBytes(buf, innerData.data(), innerData.size());
    return buf;
}

/// Build a LIST chunk: LIST header + listType + inner data.
inline std::vector<uint8_t> MakeListChunk(uint32_t listType,
                                           const std::vector<uint8_t> &innerData)
{
    std::vector<uint8_t> buf;
    AppendU32LE(buf, avi::kFourCC_LIST);
    AppendU32LE(buf, static_cast<uint32_t>(4 + innerData.size()));
    AppendU32LE(buf, listType);
    AppendBytes(buf, innerData.data(), innerData.size());
    return buf;
}

/// Build a plain (non-LIST) chunk.
inline std::vector<uint8_t> MakeChunk(uint32_t chunkId,
                                       const std::vector<uint8_t> &data)
{
    std::vector<uint8_t> buf;
    AppendU32LE(buf, chunkId);
    AppendU32LE(buf, static_cast<uint32_t>(data.size()));
    AppendBytes(buf, data.data(), data.size());
    // Pad to 2-byte boundary
    if (data.size() & 1)
        AppendU8(buf, 0);
    return buf;
}

/// Build an 'avih' chunk with the given main AVI header fields.
inline std::vector<uint8_t> MakeAvihChunk(uint32_t streams,
                                           uint32_t width, uint32_t height,
                                           uint32_t microSecPerFrame = 33333,
                                           uint32_t totalFrames = 0)
{
    std::vector<uint8_t> data;
    AppendU32LE(data, microSecPerFrame); // dwMicroSecPerFrame
    AppendU32LE(data, 0);               // dwMaxBytesPerSec
    AppendU32LE(data, 0);               // dwPaddingGranularity
    AppendU32LE(data, 0);               // dwFlags
    AppendU32LE(data, totalFrames);     // dwTotalFrames
    AppendU32LE(data, 0);               // dwInitialFrames
    AppendU32LE(data, streams);         // dwStreams
    AppendU32LE(data, 0);               // dwSuggestedBufferSize
    AppendU32LE(data, width);           // dwWidth
    AppendU32LE(data, height);          // dwHeight
    AppendZeros(data, 16);              // dwReserved[4]
    return MakeChunk(avi::kFourCC_avih, data);
}

/// Build a 'strh' (stream header) chunk.
inline std::vector<uint8_t> MakeStrhChunk(uint32_t streamType,
                                           uint32_t codecHandler,
                                           uint32_t scale, uint32_t rate,
                                           uint32_t length)
{
    std::vector<uint8_t> data;
    AppendU32LE(data, streamType);   // fccType
    AppendU32LE(data, codecHandler); // fccHandler
    AppendU32LE(data, 0);            // dwFlags
    AppendU16LE(data, 0);            // wPriority
    AppendU16LE(data, 0);            // wLanguage
    AppendU32LE(data, 0);            // dwInitialFrames
    AppendU32LE(data, scale);        // dwScale
    AppendU32LE(data, rate);         // dwRate
    AppendU32LE(data, 0);            // dwStart
    AppendU32LE(data, length);       // dwLength
    AppendU32LE(data, 0);            // dwSuggestedBufferSize
    AppendU32LE(data, 0);            // dwQuality
    AppendU32LE(data, 0);            // dwSampleSize
    AppendU16LE(data, 0);            // rcFrame.left   (int16_t, matching packed struct)
    AppendU16LE(data, 0);            // rcFrame.top
    AppendU16LE(data, 0);            // rcFrame.right
    AppendU16LE(data, 0);            // rcFrame.bottom
    return MakeChunk(avi::kFourCC_strh, data);
}

/// Build a video 'strf' chunk (BITMAPINFOHEADER, optionally with palette).
inline std::vector<uint8_t> MakeStrfVideoChunk(int32_t width, int32_t height,
                                                uint16_t bpp,
                                                uint32_t compression,
                                                const std::vector<uint32_t> &palette = {})
{
    std::vector<uint8_t> data;
    uint32_t biSize = 40;
    if (compression == avi::kCodec_BITFIELDS)
        biSize = 40 + 12; // 3 extra DWORDs for masks
    AppendU32LE(data, biSize);         // biSize
    AppendI32LE(data, width);          // biWidth
    AppendI32LE(data, height);         // biHeight (positive = bottom-up)
    AppendU16LE(data, 1);              // biPlanes
    AppendU16LE(data, bpp);            // biBitCount
    AppendU32LE(data, compression);    // biCompression
    AppendU32LE(data, 0);              // biSizeImage
    AppendI32LE(data, 0);              // biXPelsPerMeter
    AppendI32LE(data, 0);              // biYPelsPerMeter
    AppendU32LE(data, static_cast<uint32_t>(palette.size())); // biClrUsed
    AppendU32LE(data, 0);              // biClrImportant

    if (compression == avi::kCodec_BITFIELDS)
    {
        // Default 565 masks for 16bpp
        AppendU32LE(data, 0x0000F800u); // red
        AppendU32LE(data, 0x000007E0u); // green
        AppendU32LE(data, 0x0000001Fu); // blue
    }

    // Append palette entries (BGRX format)
    for (uint32_t c : palette)
    {
        uint8_t b = static_cast<uint8_t>(c & 0xFF);
        uint8_t g = static_cast<uint8_t>((c >> 8) & 0xFF);
        uint8_t r = static_cast<uint8_t>((c >> 16) & 0xFF);
        AppendU8(data, b);
        AppendU8(data, g);
        AppendU8(data, r);
        AppendU8(data, 0); // reserved
    }

    return MakeChunk(avi::kFourCC_strf, data);
}

/// Build a 'LIST strl' (stream list) for a video stream.
inline std::vector<uint8_t> MakeVideoStreamList(int32_t width, int32_t height,
                                                  uint16_t bpp,
                                                  uint32_t compression,
                                                  uint32_t scale, uint32_t rate,
                                                  uint32_t length,
                                                  const std::vector<uint32_t> &palette = {})
{
    std::vector<uint8_t> inner;
    auto strh = MakeStrhChunk(avi::kStreamType_vids, compression, scale, rate, length);
    auto strf = MakeStrfVideoChunk(width, height, bpp, compression, palette);
    AppendBytes(inner, strh.data(), strh.size());
    AppendBytes(inner, strf.data(), strf.size());
    return MakeListChunk(avi::kFourCC_strl, inner);
}

/// Build an idx1 chunk from a list of (chunkId, flags, offset, size) tuples.
struct Idx1Entry
{
    uint32_t chunkId;
    uint32_t flags;
    uint32_t offset;
    uint32_t size;
};

inline std::vector<uint8_t> MakeIdx1Chunk(const std::vector<Idx1Entry> &entries)
{
    std::vector<uint8_t> data;
    for (const auto &e : entries)
    {
        AppendU32LE(data, e.chunkId);
        AppendU32LE(data, e.flags);
        AppendU32LE(data, e.offset);
        AppendU32LE(data, e.size);
    }
    return MakeChunk(avi::kFourCC_idx1, data);
}

/// Build a complete minimal AVI file with one video stream and N frames of
/// given payload. All frames are keyframes. Returns the raw file bytes.
///
/// Frame payload bytes are taken from `framePayloads[i]`.
inline std::vector<uint8_t> BuildMinimalAvi(int32_t width, int32_t height,
                                             uint16_t bpp,
                                             uint32_t compression,
                                             uint32_t fps,
                                             const std::vector<std::vector<uint8_t>> &framePayloads,
                                             const std::vector<uint32_t> &palette = {})
{
    const uint32_t numFrames = static_cast<uint32_t>(framePayloads.size());

    // -- 'LIST hdrl' --
    std::vector<uint8_t> hdrlInner;
    auto avih = MakeAvihChunk(1, static_cast<uint32_t>(width > 0 ? width : -width),
                               static_cast<uint32_t>(height > 0 ? height : -height),
                               fps > 0 ? (1000000u / fps) : 33333u, numFrames);
    AppendBytes(hdrlInner, avih.data(), avih.size());

    auto strl = MakeVideoStreamList(width, height, bpp, compression,
                                     1, fps, numFrames, palette);
    AppendBytes(hdrlInner, strl.data(), strl.size());

    auto hdrlList = MakeListChunk(avi::kFourCC_hdrl, hdrlInner);

    // -- 'LIST movi' --
    uint32_t streamChunkId = avi::StreamChunkId(0, avi::kStreamType_vids);

    std::vector<uint8_t> moviInner;
    // Record offsets for idx1. The parser resolves offsets relative to the
    // 'movi' FourCC identifier (the 4 bytes 'movi' inside the LIST chunk).
    // Child chunks start 4 bytes after the identifier (past the formType).
    std::vector<Idx1Entry> idx1Entries;

    for (uint32_t i = 0; i < numFrames; ++i)
    {
        // +4 to skip past 'movi' formType to the actual chunk header
        uint32_t relOffset = 4u + static_cast<uint32_t>(moviInner.size());
        auto chunk = MakeChunk(streamChunkId, framePayloads[i]);
        idx1Entries.push_back({streamChunkId, avi::kAVIIF_KEYFRAME,
                                relOffset,
                                static_cast<uint32_t>(framePayloads[i].size())});
        AppendBytes(moviInner, chunk.data(), chunk.size());
    }

    auto moviList = MakeListChunk(avi::kFourCC_movi, moviInner);

    // -- idx1 --
    auto idx1 = MakeIdx1Chunk(idx1Entries);

    // -- RIFF AVI --
    std::vector<uint8_t> riffInner;
    AppendBytes(riffInner, hdrlList.data(), hdrlList.size());
    AppendBytes(riffInner, moviList.data(), moviList.size());
    AppendBytes(riffInner, idx1.data(), idx1.size());

    return MakeRiffFile(avi::kFourCC_AVI, riffInner);
}

/// Convenience: build AVI stream info for decoder tests (no file I/O).
inline avi::AviStreamInfo MakeVideoStreamInfo(uint32_t codec, int width, int height,
                                               uint16_t bpp, bool topDown = false,
                                               uint32_t redMask = 0,
                                               uint32_t greenMask = 0,
                                               uint32_t blueMask = 0,
                                               uint32_t alphaMask = 0,
                                               const std::vector<uint32_t> &palette = {})
{
    avi::AviStreamInfo info;
    info.type = avi::kStreamType_vids;
    info.codec = codec;
    info.width = width;
    info.height = height;
    info.bitsPerPixel = bpp;
    info.videoTopDown = topDown;
    info.redMask = redMask;
    info.greenMask = greenMask;
    info.blueMask = blueMask;
    info.alphaMask = alphaMask;
    info.palette = palette;
    info.frameRate = 30.0;
    info.totalFrames = 1;
    return info;
}

} // namespace testutil

#endif // AVIREADER_TESTUTILS_H
