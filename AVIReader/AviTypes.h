#ifndef AVITYPES_H
#define AVITYPES_H

#include <cstdint>
#include <cstddef>
#include <vector>

// ---------------------------------------------------------------------------
// FourCC helpers
// ---------------------------------------------------------------------------

// RIFF / AVI chunk identifiers
namespace avi {

/// Construct a FourCC code at compile time from four characters.
constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
{
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kFourCC_RIFF = MakeFourCC('R', 'I', 'F', 'F');
constexpr uint32_t kFourCC_LIST = MakeFourCC('L', 'I', 'S', 'T');
constexpr uint32_t kFourCC_JUNK = MakeFourCC('J', 'U', 'N', 'K');

constexpr uint32_t kFourCC_AVI  = MakeFourCC('A', 'V', 'I', ' ');
constexpr uint32_t kFourCC_AVIX = MakeFourCC('A', 'V', 'I', 'X');

constexpr uint32_t kFourCC_hdrl = MakeFourCC('h', 'd', 'r', 'l');
constexpr uint32_t kFourCC_avih = MakeFourCC('a', 'v', 'i', 'h');
constexpr uint32_t kFourCC_strl = MakeFourCC('s', 't', 'r', 'l');
constexpr uint32_t kFourCC_strh = MakeFourCC('s', 't', 'r', 'h');
constexpr uint32_t kFourCC_strf = MakeFourCC('s', 't', 'r', 'f');
constexpr uint32_t kFourCC_strd = MakeFourCC('s', 't', 'r', 'd');
constexpr uint32_t kFourCC_strn = MakeFourCC('s', 't', 'r', 'n');
constexpr uint32_t kFourCC_indx = MakeFourCC('i', 'n', 'd', 'x');

constexpr uint32_t kFourCC_movi = MakeFourCC('m', 'o', 'v', 'i');
constexpr uint32_t kFourCC_idx1 = MakeFourCC('i', 'd', 'x', '1');
constexpr uint32_t kFourCC_rec  = MakeFourCC('r', 'e', 'c', ' ');

// Stream types
constexpr uint32_t kStreamType_vids = MakeFourCC('v', 'i', 'd', 's');
constexpr uint32_t kStreamType_auds = MakeFourCC('a', 'u', 'd', 's');

// Common video codecs
constexpr uint32_t kCodec_RGB  = 0x00000000; // BI_RGB  (uncompressed)
constexpr uint32_t kCodec_RLE8 = 0x00000001; // BI_RLE8
constexpr uint32_t kCodec_RLE4 = 0x00000002; // BI_RLE4
constexpr uint32_t kCodec_BITFIELDS = 0x00000003; // BI_BITFIELDS
constexpr uint32_t kCodec_MJPG = MakeFourCC('M', 'J', 'P', 'G');
constexpr uint32_t kCodec_mjpg = MakeFourCC('m', 'j', 'p', 'g');
constexpr uint32_t kCodec_CRAM = MakeFourCC('C', 'R', 'A', 'M'); // Microsoft Video 1
constexpr uint32_t kCodec_MSVC = MakeFourCC('m', 's', 'v', 'c'); // Microsoft Video 1 alias
constexpr uint32_t kCodec_WHAM = MakeFourCC('W', 'H', 'A', 'M'); // Microsoft Video 1 alias
constexpr uint32_t kCodec_YUY2 = MakeFourCC('Y', 'U', 'Y', '2');
constexpr uint32_t kCodec_UYVY = MakeFourCC('U', 'Y', 'V', 'Y');

// ---------------------------------------------------------------------------
// idx1 flags
// ---------------------------------------------------------------------------
constexpr uint32_t kAVIIF_KEYFRAME = 0x00000010;

// ---------------------------------------------------------------------------
// OpenDML index constants
// ---------------------------------------------------------------------------
constexpr uint8_t kAVI_INDEX_OF_INDEXES = 0x00; // super-index
constexpr uint8_t kAVI_INDEX_OF_CHUNKS  = 0x01; // standard index

// ---------------------------------------------------------------------------
// Portable AVI header structures (packed, little-endian on disk)
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

/// Main AVI header (inside 'avih' chunk). 56 bytes.
struct MainAviHeader
{
    uint32_t dwMicroSecPerFrame;
    uint32_t dwMaxBytesPerSec;
    uint32_t dwPaddingGranularity;
    uint32_t dwFlags;
    uint32_t dwTotalFrames;
    uint32_t dwInitialFrames;
    uint32_t dwStreams;
    uint32_t dwSuggestedBufferSize;
    uint32_t dwWidth;
    uint32_t dwHeight;
    uint32_t dwReserved[4];
};

/// AVI stream header (inside 'strh' chunk). 56 bytes.
struct AviStreamHeader
{
    uint32_t fccType;
    uint32_t fccHandler;
    uint32_t dwFlags;
    uint16_t wPriority;
    uint16_t wLanguage;
    uint32_t dwInitialFrames;
    uint32_t dwScale;
    uint32_t dwRate;
    uint32_t dwStart;
    uint32_t dwLength;
    uint32_t dwSuggestedBufferSize;
    uint32_t dwQuality;
    uint32_t dwSampleSize;
    struct {
        int16_t left;
        int16_t top;
        int16_t right;
        int16_t bottom;
    } rcFrame;
};

/// Portable BITMAPINFOHEADER replacement (video 'strf'). 40 bytes.
struct BitmapInfoHeader
{
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
};

/// WAVEFORMATEX replacement (audio 'strf'). Minimum 18 bytes.
struct WaveFormatHeader
{
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize; // extra bytes after this header (may be absent on disk for PCM)
};

/// Classic AVI index entry (inside 'idx1' chunk). 16 bytes.
struct AviOldIndexEntry
{
    uint32_t dwChunkId;
    uint32_t dwFlags;
    uint32_t dwOffset;
    uint32_t dwSize;
};

/// OpenDML super-index header (inside 'indx' chunk in strl).
struct AviSuperIndexHeader
{
    uint16_t wLongsPerEntry; // must be 4
    uint8_t  bIndexSubType;  // must be 0
    uint8_t  bIndexType;     // AVI_INDEX_OF_INDEXES (0x00)
    uint32_t nEntriesInUse;
    uint32_t dwChunkId;
    uint32_t dwReserved[3];
};

/// One entry in the super-index. 16 bytes.
struct AviSuperIndexEntry
{
    uint64_t qwOffset;   // absolute file offset of standard index chunk
    uint32_t dwSize;     // size of standard index chunk (including header)
    uint32_t dwDuration; // stream ticks spanned
};

/// OpenDML standard-index header (inside 'ix##' chunk).
struct AviStdIndexHeader
{
    uint16_t wLongsPerEntry; // must be 2
    uint8_t  bIndexSubType;  // must be 0
    uint8_t  bIndexType;     // AVI_INDEX_OF_CHUNKS (0x01)
    uint32_t nEntriesInUse;
    uint32_t dwChunkId;
    uint64_t qwBaseOffset;   // all entry offsets are relative to this
    uint32_t dwReserved;
};

/// One entry in the standard index. 8 bytes.
struct AviStdIndexEntry
{
    uint32_t dwOffset; // relative to qwBaseOffset (points to data, not chunk header)
    uint32_t dwSize;   // bit 31 set = NOT a keyframe
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Unified frame-index entry (in-memory, built from idx1 or OpenDML)
// ---------------------------------------------------------------------------

struct FrameIndexEntry
{
    int64_t  offset;     ///< Absolute file offset of compressed frame data.
    uint32_t size;       ///< Size of compressed frame data in bytes.
    bool     isKeyframe; ///< True if this is a keyframe / sync point.
};

// ---------------------------------------------------------------------------
// High-level stream info (filled during header parsing)
// ---------------------------------------------------------------------------

struct AviStreamInfo
{
    uint32_t type;          ///< Stream type FourCC ('vids' / 'auds').
    uint32_t codec;         ///< Video: biCompression / handler FourCC. Audio: wFormatTag.
    int      width;         ///< Video width in pixels (0 for audio).
    int      height;        ///< Video height in pixels (0 for audio).
    bool     videoTopDown;  ///< Video bitmap is top-down on disk (negative biHeight).
    int      bitsPerPixel;  ///< Video bits per pixel (0 for audio).
    uint32_t redMask;       ///< Video red mask for BITFIELDS / high-color formats.
    uint32_t greenMask;     ///< Video green mask for BITFIELDS / high-color formats.
    uint32_t blueMask;      ///< Video blue mask for BITFIELDS / high-color formats.
    uint32_t alphaMask;     ///< Video alpha mask for BITFIELDS / high-color formats.
    std::vector<uint32_t> palette; ///< Optional ARGB palette for indexed formats.
    double   frameRate;     ///< Frames (or samples) per second.
    uint32_t totalFrames;   ///< Number of frames/samples (from strh.dwLength).

    // Audio-specific (zero for video streams)
    int      sampleRate;
    int      channels;
    int      bitsPerSample;
    int      blockAlign;

    AviStreamInfo()
        : type(0), codec(0), width(0), height(0), videoTopDown(false), bitsPerPixel(0),
          redMask(0), greenMask(0), blueMask(0), alphaMask(0),
          frameRate(0.0), totalFrames(0),
          sampleRate(0), channels(0), bitsPerSample(0), blockAlign(0)
    {}
};

// ---------------------------------------------------------------------------
// Utility: build a stream chunk-id (e.g. stream 0 video = "00dc")
// ---------------------------------------------------------------------------

/// Returns the 2-character chunk suffix for a stream type ('dc' for video, 'wb' for audio).
inline const char *StreamChunkSuffix(uint32_t streamType)
{
    if (streamType == kStreamType_vids) return "dc";
    if (streamType == kStreamType_auds) return "wb";
    return "??";
}

/// Build the 4-byte chunk id for a given stream index and type.
/// E.g. stream 0 video -> '00dc', stream 1 audio -> '01wb'.
inline uint32_t StreamChunkId(int streamIndex, uint32_t streamType)
{
    const char *suffix = StreamChunkSuffix(streamType);
    char id[4];
    id[0] = static_cast<char>('0' + (streamIndex / 10) % 10);
    id[1] = static_cast<char>('0' + streamIndex % 10);
    id[2] = suffix[0];
    id[3] = suffix[1];
    return MakeFourCC(id[0], id[1], id[2], id[3]);
}

/// Extract the stream index from a chunk id like '00dc' -> 0, '01wb' -> 1.
/// Returns -1 on invalid format.
inline int StreamIndexFromChunkId(uint32_t chunkId)
{
    const char *c = reinterpret_cast<const char *>(&chunkId);
    if (c[0] < '0' || c[0] > '9' || c[1] < '0' || c[1] > '9')
        return -1;
    return (c[0] - '0') * 10 + (c[1] - '0');
}

// ---------------------------------------------------------------------------
// Portable little-endian byte readers (from raw byte buffers, no file I/O)
// ---------------------------------------------------------------------------

inline uint16_t ReadLe16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t ReadLe32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t ReadLe64(const uint8_t *p)
{
    return static_cast<uint64_t>(p[0]) |
           (static_cast<uint64_t>(p[1]) << 8) |
           (static_cast<uint64_t>(p[2]) << 16) |
           (static_cast<uint64_t>(p[3]) << 24) |
           (static_cast<uint64_t>(p[4]) << 32) |
           (static_cast<uint64_t>(p[5]) << 40) |
           (static_cast<uint64_t>(p[6]) << 48) |
           (static_cast<uint64_t>(p[7]) << 56);
}

} // namespace avi

#endif // AVITYPES_H
