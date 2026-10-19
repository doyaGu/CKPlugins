#include "AviDemuxer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

constexpr uint32_t kMaxStreams = 64;
constexpr uint32_t kMaxFramePayloadBytes = 256u * 1024u * 1024u; // 256 MiB
constexpr uint32_t kMaxFormatChunkBytes = 4u * 1024u * 1024u;    // 4 MiB
constexpr uint32_t kMaxIdx1Entries = 4000000u;
constexpr uint32_t kMaxSuperIndexEntries = 262144u;
constexpr uint32_t kMaxStdIndexEntries = 1000000u;
constexpr size_t kMaxFramesPerStream = 4000000u;

static bool MulSizeOverflow(size_t a, size_t b, size_t &out)
{
    if (a != 0 && b > (std::numeric_limits<size_t>::max() / a))
        return true;
    out = a * b;
    return false;
}

static bool AddI64Overflow(int64_t a, int64_t b, int64_t &out)
{
    if ((b > 0 && a > (std::numeric_limits<int64_t>::max() - b)) ||
        (b < 0 && a < (std::numeric_limits<int64_t>::min() - b)))
        return true;
    out = a + b;
    return false;
}

struct ChunkIdMap
{
    uint32_t chunkId;
    int streamIdx;
};

static int FindStreamIndexByChunkId(const std::vector<ChunkIdMap> &idMap, uint32_t chunkId)
{
    for (const auto &m : idMap)
    {
        if (m.chunkId == chunkId)
            return m.streamIdx;
    }
    return -1;
}

static bool ComputeIdx1Offsets(const avi::AviOldIndexEntry &e, int64_t base, int64_t fileSize,
                        int64_t &dataOffset, int64_t &headerOffset)
{
    if (base < 0 || base > fileSize)
        return false;
    if (AddI64Overflow(base, static_cast<int64_t>(e.dwOffset) + 8, dataOffset))
        return false;
    headerOffset = dataOffset - 8;
    if (headerOffset < 0 || dataOffset < 0)
        return false;
    if (headerOffset + 8 > fileSize)
        return false;
    if (dataOffset + static_cast<int64_t>(e.dwSize) > fileSize)
        return false;
    return true;
}

static bool IsIdx1EntryOffsetHeaderValid(RiffReader &reader, const avi::AviOldIndexEntry &e,
                                  int64_t base, int64_t fileSize)
{
    int64_t dataOffset = 0;
    int64_t headerOffset = 0;
    if (!ComputeIdx1Offsets(e, base, fileSize, dataOffset, headerOffset))
        return false;

    uint8_t hdr[8];
    if (!reader.ReadAt(headerOffset, hdr, sizeof(hdr)))
        return false;

    const uint32_t chunkId   = avi::ReadLe32(hdr + 0);
    const uint32_t chunkSize = avi::ReadLe32(hdr + 4);
    if (chunkId != e.dwChunkId)
        return false;
    if (chunkSize < e.dwSize)
        return false;
    return true;
}

static bool ResolveIdx1EntryBaseByHeader(RiffReader &reader, const avi::AviOldIndexEntry &e,
                                  int64_t preferredBase, bool haveMoviBase, int64_t moviIdOffset,
                                  int64_t fileSize, int64_t &resolvedBase)
{
    resolvedBase = preferredBase;
    if (IsIdx1EntryOffsetHeaderValid(reader, e, resolvedBase, fileSize))
        return true;

    const int64_t altBase = (resolvedBase == 0 && haveMoviBase) ? moviIdOffset : 0;
    if (!IsIdx1EntryOffsetHeaderValid(reader, e, altBase, fileSize))
        return false;
    resolvedBase = altBase;
    return true;
}

// ===========================================================================
// Construction / destruction
// ===========================================================================

AviDemuxer::AviDemuxer()
    : m_Open(false), m_HasAvix(false),
      m_PaletteChanges(nullptr), m_LastPaletteChange(nullptr),
      m_PaletteIndexes(nullptr),
      m_PaletteSteps(nullptr), m_LastPaletteStep(nullptr),
      m_MoviDataOffset(0), m_MoviEndOffset(0),
      m_Idx1Offset(0), m_Idx1Size(0)
{
    memset(&m_MainHeader, 0, sizeof(m_MainHeader));
}

AviDemuxer::~AviDemuxer()
{
    Close();
}

// ===========================================================================
// Open / Close
// ===========================================================================

bool AviDemuxer::Open(const char *filename)
{
    Close();

    try
    {
        if (!m_Reader.Open(filename))
            return false;

        if (!ParseRiff())
        {
            Close();
            return false;
        }

        bool indexedVideo = false;
        for (size_t i = 0; i < m_Streams.size(); ++i)
            if (m_Streams[i].info.type == avi::kStreamType_vids &&
                m_Streams[i].info.bitsPerPixel > 0 && m_Streams[i].info.bitsPerPixel <= 8)
                indexedVideo = true;
        if (indexedVideo)
        {
            if (!ScanMovieLists(true))
            {
                Close();
                return false;
            }
        }
        if (!BuildPaletteIndexes() || !BuildIndex())
        {
            Close();
            return false;
        }

    }
    catch (...)
    {
        Close();
        return false;
    }

    m_Open = true;
    return true;
}

void AviDemuxer::Close()
{
    while (m_PaletteIndexes)
    {
        PaletteIndex *next = m_PaletteIndexes->next;
        delete[] m_PaletteIndexes->changes;
        delete m_PaletteIndexes;
        m_PaletteIndexes = next;
    }
    while (m_PaletteSteps)
    {
        PaletteStep *next = m_PaletteSteps->next;
        delete m_PaletteSteps;
        m_PaletteSteps = next;
    }
    m_LastPaletteStep = nullptr;
    while (m_PaletteChanges)
    {
        PaletteChange *next = m_PaletteChanges->next;
        delete m_PaletteChanges;
        m_PaletteChanges = next;
    }
    m_LastPaletteChange = nullptr;
    m_Reader.Close();
    m_Open = false;
    m_HasAvix = false;
    m_Streams.clear();
    memset(&m_MainHeader, 0, sizeof(m_MainHeader));
    m_MoviDataOffset = 0;
    m_MoviEndOffset = 0;
    m_Idx1Offset = 0;
    m_Idx1Size = 0;
}

// ===========================================================================
// Stream queries
// ===========================================================================

const avi::AviStreamInfo *AviDemuxer::GetStreamInfo(int streamIndex) const
{
    if (streamIndex < 0 || streamIndex >= static_cast<int>(m_Streams.size()))
        return nullptr;
    return &m_Streams[streamIndex].info;
}

int AviDemuxer::FindFirstVideoStream() const
{
    for (int i = 0; i < static_cast<int>(m_Streams.size()); ++i)
    {
        if (m_Streams[i].info.type == avi::kStreamType_vids)
            return i;
    }
    return -1;
}

int AviDemuxer::FindFirstAudioStream() const
{
    for (int i = 0; i < static_cast<int>(m_Streams.size()); ++i)
    {
        if (m_Streams[i].info.type == avi::kStreamType_auds)
            return i;
    }
    return -1;
}

// ===========================================================================
// Frame queries
// ===========================================================================

int AviDemuxer::GetFrameCount(int streamIndex) const
{
    if (streamIndex < 0 || streamIndex >= static_cast<int>(m_Streams.size()))
        return 0;
    return static_cast<int>(m_Streams[streamIndex].index.size());
}

int AviDemuxer::GetDurationMs(int streamIndex) const
{
    const avi::AviStreamInfo *info = GetStreamInfo(streamIndex);
    if (!info || info->frameRate <= 0.0)
        return 0;
    const int frames = GetFrameCount(streamIndex);
    return static_cast<int>((frames / info->frameRate) * 1000.0 + 0.5);
}

const avi::FrameIndexEntry *AviDemuxer::GetFrameInfo(int streamIndex, int frameIndex) const
{
    if (streamIndex < 0 || streamIndex >= static_cast<int>(m_Streams.size()))
        return nullptr;
    const auto &idx = m_Streams[streamIndex].index;
    if (frameIndex < 0 || frameIndex >= static_cast<int>(idx.size()))
        return nullptr;
    return &idx[frameIndex];
}

// ===========================================================================
// Frame data access
// ===========================================================================

bool AviDemuxer::ReadFrameData(int streamIndex, int frameIndex, std::vector<uint8_t> &outData)
{
    const avi::FrameIndexEntry *entry = GetFrameInfo(streamIndex, frameIndex);
    if (!entry || entry->size > kMaxFramePayloadBytes)
    {
        outData.clear();
        return false;
    }

    if (entry->size == 0)
    {
        outData.clear();
        return true;
    }

    if (entry->offset < 0)
    {
        outData.clear();
        return false;
    }

    int64_t endOffset = 0;
    if (AddI64Overflow(entry->offset, static_cast<int64_t>(entry->size), endOffset) ||
        endOffset > m_Reader.FileSize())
    {
        outData.clear();
        return false;
    }

    try
    {
        outData.resize(entry->size);
    }
    catch (...)
    {
        outData.clear();
        return false;
    }

    return m_Reader.ReadAt(entry->offset, outData.data(), entry->size);
}

// ===========================================================================
// Top-level RIFF parse
// ===========================================================================

bool AviDemuxer::ParseRiff()
{
    m_Reader.SeekTo(0);

    // Read the main RIFF header -- must be 'RIFF' / 'AVI '.
    RiffReader::Chunk riffChunk;
    if (!m_Reader.ReadChunkHeader(riffChunk))
        return false;
    if (riffChunk.id != avi::kFourCC_RIFF || riffChunk.listType != avi::kFourCC_AVI)
        return false;

    if (!m_Reader.DescendInto(riffChunk))
        return false;

    bool foundHdrl = false;
    bool foundMovi = false;

    // Iterate top-level children of the RIFF AVI container.
    RiffReader::Chunk child;
    while (m_Reader.ReadChunkHeader(child))
    {
        if (child.IsList() && child.listType == avi::kFourCC_hdrl)
        {
            if (!ParseHeaderList(child))
                return false;
            foundHdrl = true;
        }
        else if (child.IsList() && child.listType == avi::kFourCC_movi)
        {
            // Record the location of the first 'movi' list for idx1 offset base.
            if (!foundMovi)
            {
                m_MoviDataOffset = child.dataOffset;
                // End of the LIST movi data region.
                int64_t moviListStart = child.dataOffset - 4;
                if (moviListStart < 0 ||
                    AddI64Overflow(moviListStart, static_cast<int64_t>(child.size), m_MoviEndOffset))
                    return false;
            }
            foundMovi = true;
        }
        else if (child.id == avi::kFourCC_idx1)
        {
            // Record the idx1 location for later use.
            m_Idx1Offset = child.dataOffset;
            m_Idx1Size = child.size;
        }

        // Skip to next sibling
        if (!m_Reader.SkipChunk(child))
            return false;
    }

    if (!m_Reader.Ascend())
        return false;

    // A classic idx1 only describes the initial RIFF, while palette changes
    // and recoverable frame chunks can continue in later RIFF AVIX segments.
    if (!m_Reader.SkipChunk(riffChunk))
        return false;
    while (m_Reader.Tell() < m_Reader.FileSize())
    {
        RiffReader::Chunk continuation;
        if (!m_Reader.ReadChunkHeader(continuation))
            return false;
        if (continuation.id == avi::kFourCC_RIFF && continuation.listType == avi::kFourCC_AVIX)
            m_HasAvix = true;
        if (!m_Reader.SkipChunk(continuation))
            return false;
    }

    return foundHdrl && foundMovi;
}

// ===========================================================================
// Header list parsing
// ===========================================================================

bool AviDemuxer::ParseHeaderList(const RiffReader::Chunk &hdrlChunk)
{
    if (!m_Reader.DescendInto(hdrlChunk))
        return false;

    RiffReader::Chunk child;
    bool foundAvih = false;

    while (m_Reader.ReadChunkHeader(child))
    {
        if (child.id == avi::kFourCC_avih && !foundAvih)
        {
            if (child.size < sizeof(avi::MainAviHeader))
                return false;
            // Read main AVI header (only the first 56 bytes we care about).
            const size_t toRead = std::min(static_cast<size_t>(child.size),
                                           sizeof(avi::MainAviHeader));
            memset(&m_MainHeader, 0, sizeof(m_MainHeader));
            if (!m_Reader.Read(&m_MainHeader, toRead))
                return false;
            foundAvih = true;
        }
        else if (child.IsList() && child.listType == avi::kFourCC_strl)
        {
            if (!ParseStreamList(child))
                return false;
        }

        if (!m_Reader.SkipChunk(child))
            return false;
    }

    if (!m_Reader.Ascend())
        return false;
    if (!foundAvih)
        return false;
    if (m_MainHeader.dwStreams > kMaxStreams)
        return false;

    // Specification: number of stream lists in hdrl must match avih.dwStreams.
    if (m_MainHeader.dwStreams != 0 &&
        static_cast<uint32_t>(m_Streams.size()) != m_MainHeader.dwStreams)
    {
        return false;
    }

    return true;
}

bool AviDemuxer::ParseStreamList(const RiffReader::Chunk &strlChunk)
{
    if (!m_Reader.DescendInto(strlChunk))
        return false;

    StreamEntry stream;
    bool foundStrh = false;
    bool foundStrf = false;
    avi::AviStreamHeader strh;
    memset(&strh, 0, sizeof(strh));

    RiffReader::Chunk child;
    while (m_Reader.ReadChunkHeader(child))
    {
        if (child.id == avi::kFourCC_strh && !foundStrh)
        {
            if (!ParseStreamHeader(child, strh))
                return false;
            foundStrh = true;

            // Fill common stream info from strh.
            stream.info.type = strh.fccType;
            stream.info.codec = strh.fccHandler;
            stream.info.totalFrames = strh.dwLength;
            if (strh.dwScale > 0)
                stream.info.frameRate = static_cast<double>(strh.dwRate) / static_cast<double>(strh.dwScale);
        }
        else if (child.id == avi::kFourCC_strf && foundStrh && !foundStrf)
        {
            if (strh.fccType == avi::kStreamType_vids)
            {
                avi::BitmapInfoHeader bih;
                if (!ParseVideoFormat(child, bih, stream))
                    return false;
                stream.info.width = (bih.biWidth > 0) ? bih.biWidth : 0;
                stream.info.height = (bih.biHeight >= 0) ? bih.biHeight : -bih.biHeight;
                stream.info.videoTopDown = (bih.biHeight < 0);
                stream.info.bitsPerPixel = bih.biBitCount;
                // BI_RGB is zero, even when the stream handler is "DIB ".
                stream.info.codec = bih.biCompression;
                if (bih.biCompression == avi::kCodec_YUY2 || bih.biCompression == avi::kCodec_UYVY)
                    stream.info.videoTopDown = true;
            }
            else if (strh.fccType == avi::kStreamType_auds)
            {
                avi::WaveFormatHeader wfh;
                if (!ParseAudioFormat(child, wfh))
                    return false;
                stream.info.codec = wfh.wFormatTag;
                stream.info.sampleRate = wfh.nSamplesPerSec;
                stream.info.channels = wfh.nChannels;
                stream.info.bitsPerSample = wfh.wBitsPerSample;
                stream.info.blockAlign = wfh.nBlockAlign;
            }
            foundStrf = true;
        }
        else if (child.id == avi::MakeFourCC('v', 'p', 'r', 'p') && foundStrh &&
                 strh.fccType == avi::kStreamType_vids)
        {
            if (!ParseVideoProperties(child, stream))
                return false;
        }
        else if (child.id == avi::kFourCC_indx && foundStrh)
        {
            if (!ParseStreamIndex(child, stream))
            {
                // An optional index is not the stream format. Discard its
                // partial state and let another index or movi recover frames.
                stream.headerIndex.clear();
                stream.superIndex.clear();
                stream.superIndexChunkId = 0;
            }
        }

        if (!m_Reader.SkipChunk(child))
            return false;
    }

    if (!m_Reader.Ascend())
        return false;

    if (foundStrh && foundStrf)
    {
        if (m_Streams.size() >= kMaxStreams)
            return false;
        m_Streams.push_back(std::move(stream));
        return true;
    }
    return false;
}

// ===========================================================================
// Individual chunk parsers
// ===========================================================================

bool AviDemuxer::ParseStreamHeader(const RiffReader::Chunk &chunk, avi::AviStreamHeader &strh)
{
    if (chunk.size < sizeof(avi::AviStreamHeader))
        return false;
    const size_t toRead = std::min(static_cast<size_t>(chunk.size),
                                   sizeof(avi::AviStreamHeader));
    memset(&strh, 0, sizeof(strh));
    return m_Reader.Read(&strh, toRead);
}

bool AviDemuxer::ParseVideoFormat(const RiffReader::Chunk &chunk, avi::BitmapInfoHeader &bih, StreamEntry &stream)
{
    memset(&bih, 0, sizeof(bih));
    stream.info.redMask = 0;
    stream.info.greenMask = 0;
    stream.info.blueMask = 0;
    stream.info.alphaMask = 0;
    stream.info.palette.clear();

    if (chunk.size < sizeof(avi::BitmapInfoHeader) || chunk.size > kMaxFormatChunkBytes)
        return false;

    std::vector<uint8_t> fmt;
    try
    {
        fmt.assign(chunk.size, 0);
    }
    catch (...)
    {
        return false;
    }

    if (!m_Reader.Read(fmt.data(), fmt.size()))
        return false;

    memcpy(&bih, fmt.data(), sizeof(avi::BitmapInfoHeader));

    // Parse optional color masks.
    if (bih.biCompression == avi::kCodec_BITFIELDS && fmt.size() >= sizeof(avi::BitmapInfoHeader) + 12)
    {
        const size_t m = sizeof(avi::BitmapInfoHeader);
        stream.info.redMask   = avi::ReadLe32(&fmt[m + 0]);
        stream.info.greenMask = avi::ReadLe32(&fmt[m + 4]);
        stream.info.blueMask  = avi::ReadLe32(&fmt[m + 8]);
    }
    else if (bih.biCompression == avi::kCodec_RGB && bih.biBitCount == 16)
    {
        // Default DIB 16bpp masks (5-5-5) when no bitfields are present.
        stream.info.redMask = 0x00007C00u;
        stream.info.greenMask = 0x000003E0u;
        stream.info.blueMask = 0x0000001Fu;
    }
    else if (bih.biCompression == avi::kCodec_RGB && bih.biBitCount == 32)
    {
        stream.info.redMask = 0x00FF0000u;
        stream.info.greenMask = 0x0000FF00u;
        stream.info.blueMask = 0x000000FFu;
        // BI_RGB 32bpp is usually BGRX (opaque), not BGRA.
        stream.info.alphaMask = 0;
    }

    // Parse optional palette for indexed DIB formats.
    if (bih.biBitCount <= 8)
    {
        const uint32_t maxColors = 1u << bih.biBitCount;
        uint32_t colorCount = bih.biClrUsed;
        if (colorCount == 0 || colorCount > maxColors)
            colorCount = maxColors;

        size_t paletteOffset = static_cast<size_t>(bih.biSize);
        if (paletteOffset < sizeof(avi::BitmapInfoHeader))
            paletteOffset = sizeof(avi::BitmapInfoHeader);
        size_t paletteBytes = 0;
        if (!MulSizeOverflow(static_cast<size_t>(colorCount), 4u, paletteBytes) &&
            paletteOffset <= fmt.size() &&
            paletteBytes <= (fmt.size() - paletteOffset))
        {
            try
            {
                stream.info.palette.resize(colorCount);
            }
            catch (...)
            {
                return false;
            }
            for (uint32_t i = 0; i < colorCount; ++i)
            {
                const uint8_t *c = &fmt[paletteOffset + static_cast<size_t>(i) * 4];
                const uint32_t b = c[0];
                const uint32_t g = c[1];
                const uint32_t r = c[2];
                stream.info.palette[i] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
    }

    return true;
}

bool AviDemuxer::ParseVideoProperties(const RiffReader::Chunk &chunk, StreamEntry &stream)
{
    uint8_t bytes[100]; // 36-byte header and up to two 32-byte field descriptors.
    if (chunk.size < 36 || !m_Reader.Read(bytes, std::min(size_t(chunk.size), sizeof(bytes))))
        return false;
    const unsigned count = avi::ReadLe32(bytes + 32);
    if (count == 0)
        return true;
    if (count > 2 || chunk.size < 36 + count * 32)
        return false;
    AviVideoLayout layout;
    layout.fields = count;
    for (unsigned i = 0; i < count; ++i)
    {
        const uint8_t *p = bytes + 36 + i * 32;
        AviVideoField &f = layout.field[i];
        f.compressedHeight = avi::ReadLe32(p);
        f.compressedWidth = avi::ReadLe32(p + 4);
        f.height = avi::ReadLe32(p + 8);
        f.width = avi::ReadLe32(p + 12);
        f.x = avi::ReadLe32(p + 16);
        f.y = avi::ReadLe32(p + 20);
        f.signalX = avi::ReadLe32(p + 24);
        f.signalY = avi::ReadLe32(p + 28);
        if (!f.width || !f.height || f.compressedWidth > 16384 || f.compressedHeight > 16384 ||
            f.width > f.compressedWidth || f.height > f.compressedHeight ||
            f.x > f.compressedWidth - f.width || f.y > f.compressedHeight - f.height)
            return false;
    }
    layout.width = int(layout.field[0].width);
    layout.height = int(layout.field[0].height);
    if (count == 2)
    {
        // Without complete broadcast timing, line numbers establish field order only.
        if (layout.field[0].signalY != layout.field[1].signalY)
            layout.firstLine = layout.field[0].signalY < layout.field[1].signalY ? 0 : 1;
        // Crop away the common HSync origin, retaining the fields' relative
        // horizontal positions. Bitmap crop offsets select source pixels only.
        const uint32_t origin = std::min(layout.field[0].signalX, layout.field[1].signalX);
        layout.width = layout.height = 0;
        for (unsigned i = 0; i < 2; ++i)
        {
            AviVideoField &f = layout.field[i];
            f.outputX = f.signalX - origin;
            const uint64_t right = uint64_t(f.outputX) + f.width;
            if (right > uint64_t(std::numeric_limits<int>::max() / 4))
                return false;
            layout.width = std::max(layout.width, int(right));
            const int line = i == 0 ? layout.firstLine : 1 - layout.firstLine;
            f.outputY = line;
            layout.height = std::max(layout.height, line + int(f.height) * 2 - 1);
        }
    }
    if (count == 2)
    {
        const uint32_t total = avi::ReadLe32(bytes + 16);
        const uint32_t canvasHeight = avi::ReadLe32(bytes + 28);
        const uint32_t first = layout.field[0].signalY, second = layout.field[1].signalY;
        if ((total == 525 || total == 625) && canvasHeight &&
            first && first <= total && second && second <= total)
        {
            // BT.656-4 field identities: 525 field two wraps through lines 1..3.
            const int firstPhase = total == 525 ? (first < 4 || first >= 266) : first >= 313;
            const int secondPhase = total == 525 ? (second < 4 || second >= 266) : second >= 313;
            if (firstPhase != secondPhase)
            {
                if (canvasHeight > 16384)
                    return false;
                layout.firstLine = firstPhase;
                layout.height = int(canvasHeight);
                // The canonical active canvas starts at line 20 (525) or 23
                // (625). Keep twice-line positions until subtracting the odd
                // signal total, preserving the second field's half-line phase.
                int origin = total == 525 ? 40 : 46;
                for (unsigned i = 0; i < 2; ++i)
                {
                    AviVideoField &f = layout.field[i];
                    const int phase = i == 0 ? firstPhase : secondPhase;
                    int position = int(f.signalY) * 2;
                    if (phase)
                        position += total == 525 && f.signalY < 4 ? int(total) : -int(total);
                    f.outputY = position;
                }
                if (total == 525)
                {
                    // vprp does not identify the older NTSC V-blanking revision.
                    // Retain both valid fields when an even canvas origin can
                    // do so, choosing the closest origin to the modern default.
                    const AviVideoField &a = layout.field[0], &b = layout.field[1];
                    const int bottom = std::max(a.outputY + int(a.height) * 2 - 1,
                                              b.outputY + int(b.height) * 2 - 1);
                    int low = bottom - layout.height;
                    low += low % 2 != 0;
                    int high = std::min(a.outputY, b.outputY);
                    high -= high % 2;
                    if (low <= high)
                        origin = std::max(low, std::min(origin, high));
                }
                for (unsigned i = 0; i < 2; ++i)
                    layout.field[i].outputY -= origin;
            }
        }
    }
    stream.info.videoLayout = layout;
    return true;
}

bool AviDemuxer::ParseAudioFormat(const RiffReader::Chunk &chunk, avi::WaveFormatHeader &wfh)
{
    if (chunk.size < 16)
        return false;
    const size_t toRead = std::min(static_cast<size_t>(chunk.size),
                                   sizeof(avi::WaveFormatHeader));
    memset(&wfh, 0, sizeof(wfh));
    return m_Reader.Read(&wfh, toRead);
}

bool AviDemuxer::ParseStreamIndex(const RiffReader::Chunk &chunk, StreamEntry &stream)
{
    uint8_t common[12];
    if (chunk.size >= sizeof(common) && m_Reader.ReadAt(chunk.dataOffset, common, sizeof(common)) &&
        (avi::ReadLe32(common + 8) >> 16) == ('p' | ('c' << 8)))
    {
        // OpenDML puts palette changes in their own index list. It must never
        // append to, replace, or invalidate the image index of this stream.
        PaletteIndex *index = new PaletteIndex;
        index->next = m_PaletteIndexes;
        index->stream = static_cast<int>(m_Streams.size());
        index->offset = chunk.dataOffset - 8;
        index->changes = nullptr;
        index->count = 0;
        m_PaletteIndexes = index;
        return true;
    }
    if (chunk.size < sizeof(avi::AviSuperIndexHeader))
        return false;
    if (!m_Reader.SeekTo(chunk.dataOffset))
        return false;

    avi::AviSuperIndexHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    if (!m_Reader.Read(&hdr, sizeof(hdr)))
        return false;

    if (hdr.bIndexSubType != 0 && hdr.bIndexSubType != avi::kAVI_INDEX_2FIELD)
        return false;

    if (!m_Reader.SeekTo(chunk.dataOffset))
        return false;

    if (hdr.bIndexType == avi::kAVI_INDEX_OF_INDEXES)
        return ParseSuperIndex(chunk, stream);

    if (hdr.bIndexType == avi::kAVI_INDEX_OF_CHUNKS)
    {
        const int64_t chunkOffset = chunk.dataOffset - 8;
        if (chunkOffset < 0)
            return false;
        return ParseStandardIndex(chunkOffset, chunk.size + 8u, hdr.dwChunkId, stream.headerIndex);
    }

    return false;
}

bool AviDemuxer::ParseSuperIndex(const RiffReader::Chunk &chunk, StreamEntry &stream)
{
    if (chunk.size < sizeof(avi::AviSuperIndexHeader))
        return false;

    avi::AviSuperIndexHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    if (!m_Reader.Read(&hdr, sizeof(hdr)))
        return false;

    if (hdr.bIndexType != avi::kAVI_INDEX_OF_INDEXES)
        return false;
    if (hdr.bIndexSubType != 0 && hdr.bIndexSubType != avi::kAVI_INDEX_2FIELD)
        return false;
    if (hdr.wLongsPerEntry != 4)
        return false;

    stream.superIndexChunkId = hdr.dwChunkId;

    const uint32_t count = hdr.nEntriesInUse;
    if (count > kMaxSuperIndexEntries)
        return false;
    size_t entryBytes = 0;
    if (MulSizeOverflow(static_cast<size_t>(count), sizeof(avi::AviSuperIndexEntry), entryBytes))
        return false;
    if (entryBytes > static_cast<size_t>(chunk.size) - sizeof(avi::AviSuperIndexHeader))
        return false;

    try
    {
        stream.superIndex.resize(count);
    }
    catch (...)
    {
        return false;
    }
    return m_Reader.Read(stream.superIndex.data(), entryBytes);
}

// ===========================================================================
// Index building
// ===========================================================================

struct AviPhysicalIndex
{
    avi::FrameIndexEntry *frames = nullptr;
    unsigned char *seen = nullptr;
    size_t count = 0;
    ~AviPhysicalIndex() { delete[] frames; delete[] seen; }
};

static bool MatchesPhysicalIndex(const avi::FrameIndexEntry *entries, size_t count,
                                 AviPhysicalIndex &physical, bool mustUse, uint32_t declaredCount)
{
    if (!count || (mustUse && declaredCount && count != declaredCount))
        return false;
    if (physical.count)
        memset(physical.seen, 0, physical.count);
    bool virtualDrop = false;
    for (size_t i = 0; i < count; ++i)
    {
        const avi::FrameIndexEntry &frame = entries[i];
        if (!frame.offset && !frame.size)
        {
            virtualDrop = true;
            continue;
        }
        size_t low = 0, high = physical.count;
        while (low < high)
        {
            const size_t middle = low + (high - low) / 2;
            if (physical.frames[middle].offset < frame.offset)
                low = middle + 1;
            else
                high = middle;
        }
        if (low == physical.count || physical.frames[low].offset != frame.offset ||
            physical.frames[low].size != frame.size || (!mustUse && physical.seen[low]))
            return false;
        physical.seen[low] = 1;
    }
    if (virtualDrop && declaredCount && count != declaredCount)
        return false;
    if (!mustUse)
        for (size_t i = 0; i < physical.count; ++i)
            if (!physical.seen[i])
                return false;
    return true;
}

bool AviDemuxer::BuildIndex()
{
    // Physical chunk boundaries establish ownership and completeness. Index
    // order and keyframe flags are retained only from a valid candidate.
    if (!BuildMoviIndex())
        return false;
    AviPhysicalIndex physical[kMaxStreams];
    bool selected[kMaxStreams] = {};
    const bool mustUse = (m_MainHeader.dwFlags & 0x20u) != 0; // AVIF_MUSTUSEINDEX
    for (size_t i = 0; i < m_Streams.size(); ++i)
    {
        AviPhysicalIndex &p = physical[i];
        p.count = m_Streams[i].index.size();
        if (p.count)
        {
            p.frames = new (std::nothrow) avi::FrameIndexEntry[p.count];
            p.seen = new (std::nothrow) unsigned char[p.count];
            if (!p.frames || !p.seen)
                return false;
            memcpy(p.frames, m_Streams[i].index.data(), p.count * sizeof(*p.frames));
        }
    }
    BuildOpenDmlIndex();
    for (size_t i = 0; i < m_Streams.size(); ++i)
    {
        StreamEntry &stream = m_Streams[i];
        selected[i] = MatchesPhysicalIndex(stream.index.data(), stream.index.size(), physical[i],
                                           mustUse, stream.info.type == avi::kStreamType_vids ? stream.info.totalFrames : 0);
        if (selected[i])
            stream.headerIndex.swap(stream.index);
        else
            stream.headerIndex.clear();
        stream.index.clear();
    }
    BuildIdx1Index();
    for (size_t i = 0; i < m_Streams.size(); ++i)
    {
        StreamEntry &stream = m_Streams[i];
        if (selected[i])
            stream.headerIndex.swap(stream.index);
        else if (!MatchesPhysicalIndex(stream.index.data(), stream.index.size(), physical[i],
                                        mustUse, stream.info.type == avi::kStreamType_vids ? stream.info.totalFrames : 0))
        {
            if (mustUse && physical[i].count)
                return false; // The required presentation order cannot be inferred.
            stream.index.resize(physical[i].count);
            if (physical[i].count)
                memcpy(stream.index.data(), physical[i].frames, physical[i].count * sizeof(*physical[i].frames));
        }
    }
    bool anyFrames = false;
    for (size_t i = 0; i < m_Streams.size(); ++i)
    {
        StreamEntry &stream = m_Streams[i];
        int64_t anchor = m_MoviDataOffset;
        for (size_t f = 0; f < stream.index.size(); ++f)
            if (stream.index[f].offset)
            {
                anchor = stream.index[f].offset;
                break;
            }
        // Without explicit palette events, a virtual slot holds the preceding
        // logical sample's state. Leading slots use the first physical sample.
        for (size_t f = 0; f < stream.index.size(); ++f)
        {
            avi::FrameIndexEntry &frame = stream.index[f];
            if (frame.offset)
                anchor = frame.offset;
            else
                frame.paletteOffset = anchor;
        }
        anyFrames = anyFrames || !stream.index.empty();
    }
    return anyFrames;
}

bool AviDemuxer::GetFramePalette(int streamIndex, int frameIndex, uint32_t *colors) const
{
    const avi::AviStreamInfo *info = GetStreamInfo(streamIndex);
    const avi::FrameIndexEntry *frame = GetFrameInfo(streamIndex, frameIndex);
    if (!info || !frame)
        return false;
    for (unsigned i = 0; i < 256; ++i)
        colors[i] = i < info->palette.size() ? info->palette[i] : (0xff000000u | i * 0x010101u);
    if (frame->paletteStep >= 0)
    {
        bool valid = true;
        const unsigned visibleColors = info->bitsPerPixel > 0 && info->bitsPerPixel <= 8 ?
            1u << info->bitsPerPixel : 256;
        for (const PaletteStep *step = m_PaletteSteps; step; step = step->next)
        {
            if (step->sequence > unsigned(frame->paletteStep))
                break;
            if (step->stream != streamIndex)
                continue;
            const PaletteChange *change = step->change;
            if (!change)
                valid = false;
            else
            {
                memcpy(colors + change->first, change->colors, change->count * sizeof(uint32_t));
                if (change->first == 0 && change->count >= visibleColors)
                    valid = true;
            }
        }
        return valid;
    }
    const int64_t anchor = frame->paletteOffset >= 0 ? frame->paletteOffset : frame->offset;
    for (const PaletteIndex *index = m_PaletteIndexes; index; index = index->next)
    {
        if (index->stream != streamIndex || !index->changes)
            continue;
        // An untimed palette index locates events; physical movi positions
        // bind them to samples. It does not timestamp otherwise virtual slots.
        for (size_t i = 0; i < index->count && index->changes[i]->offset <= anchor; ++i)
        {
            const PaletteChange *change = index->changes[i];
            memcpy(colors + change->first, change->colors, change->count * sizeof(uint32_t));
        }
        return true;
    }
    for (const PaletteChange *change = m_PaletteChanges; change; change = change->next)
    {
        if (change->offset > anchor)
            break;
        if (change->stream == streamIndex)
            memcpy(colors + change->first, change->colors, change->count * sizeof(uint32_t));
    }
    return true;
}

bool AviDemuxer::ScanPaletteList(const RiffReader::Chunk &list)
{
    if (!m_Reader.DescendInto(list))
        return false;
    const int64_t end = list.dataOffset - 4 + list.size;
    while (m_Reader.Tell() < end)
    {
        RiffReader::Chunk child;
        if (!m_Reader.ReadChunkHeader(child))
            return false;
        if (child.IsList() && child.listType == avi::kFourCC_rec)
        {
            if (!ScanPaletteList(child))
                return false;
        }
        else if ((child.id >> 16) == ('p' | ('c' << 8)))
        {
            const int stream = avi::StreamIndexFromChunkId(child.id);
            if (stream >= 0 && stream < static_cast<int>(m_Streams.size()) &&
                m_Streams[stream].info.type == avi::kStreamType_vids &&
                m_Streams[stream].info.bitsPerPixel > 0 && m_Streams[stream].info.bitsPerPixel <= 8)
            {
                uint8_t payload[1028];
                if (child.size < 4 || child.size > sizeof(payload) || !m_Reader.Read(payload, child.size))
                    return false;
                const unsigned first = payload[0];
                const unsigned count = payload[1] ? payload[1] : 256;
                if (first + count > 256 || child.size < 4 + count * 4)
                    return false;
                PaletteChange *change = new PaletteChange;
                change->next = nullptr;
                change->stream = stream;
                change->offset = child.dataOffset;
                change->size = child.size;
                change->first = first;
                change->count = count;
                for (unsigned i = 0; i < count; ++i)
                {
                    // AVIPALCHANGE contains PALETTEENTRY (R,G,B,flags), not RGBQUAD.
                    const uint8_t *rgb = payload + 4 + i * 4;
                    change->colors[i] = 0xff000000u | (uint32_t(rgb[0]) << 16) |
                        (uint32_t(rgb[1]) << 8) | rgb[2];
                }
                if (m_LastPaletteChange)
                    m_LastPaletteChange->next = change;
                else
                    m_PaletteChanges = change;
                m_LastPaletteChange = change;
            }
        }
        if (!m_Reader.SkipChunk(child))
            return false;
    }
    return m_Reader.Ascend();
}

bool AviDemuxer::BuildPaletteIndexes()
{
    for (PaletteIndex *index = m_PaletteIndexes; index; index = index->next)
    {
        if (index->stream >= static_cast<int>(m_Streams.size()))
            continue;
        const avi::AviStreamInfo &info = m_Streams[index->stream].info;
        if (info.type != avi::kStreamType_vids || info.bitsPerPixel <= 0 || info.bitsPerPixel > 8)
            continue;
        size_t count = 0;
        for (PaletteChange *p = m_PaletteChanges; p; p = p->next)
            if (p->stream == index->stream)
                ++count;
        if (!count)
            continue;
        PaletteChange **changes = new (std::nothrow) PaletteChange *[count];
        unsigned char *seen = new (std::nothrow) unsigned char[count];
        if (!changes || !seen)
        {
            delete[] changes;
            delete[] seen;
            return false;
        }
        size_t used = 0;
        for (PaletteChange *p = m_PaletteChanges; p; p = p->next)
            if (p->stream == index->stream)
                changes[used++] = p;
        memset(seen, 0, count);
        used = 0;
        const bool valid = ParsePaletteIndex(index->offset, index->stream, changes, seen, count, used, true);
        delete[] seen;
        if (valid && used == count)
        {
            index->changes = changes;
            index->count = count;
        }
        else
            delete[] changes; // Optional incomplete/bad lists fall back to real movi events.
    }
    return true;
}

bool AviDemuxer::ParsePaletteIndex(int64_t offset, int stream, PaletteChange **changes,
                                  unsigned char *seen, size_t count, size_t &used, bool allowSuper)
{
    uint8_t header[32];
    if (!m_Reader.ReadAt(offset, header, sizeof(header)))
        return false;
    const uint32_t id = avi::ReadLe32(header);
    const uint32_t size = avi::ReadLe32(header + 4);
    const uint32_t ixId = avi::MakeFourCC('i', 'x', char('0' + stream / 10), char('0' + stream % 10));
    const uint32_t pcId = avi::MakeFourCC(char('0' + stream / 10), char('0' + stream % 10), 'p', 'c');
    if ((id != avi::kFourCC_indx && id != ixId) || size < 24 ||
        uint64_t(offset) + 8u + size > uint64_t(m_Reader.FileSize()) ||
        header[10] != 0 || avi::ReadLe32(header + 16) != pcId)
        return false;
    const uint32_t entries = avi::ReadLe32(header + 12);
    const bool super = header[11] == avi::kAVI_INDEX_OF_INDEXES;
    const unsigned stride = super ? 16 : 8;
    if (avi::ReadLe16(header + 8) != (super ? 4 : 2) ||
        (!super && header[11] != avi::kAVI_INDEX_OF_CHUNKS) ||
        (super && !allowSuper) || entries > (size - 24) / stride ||
        entries > (super ? kMaxSuperIndexEntries : kMaxStdIndexEntries))
        return false;
    const uint64_t base = avi::ReadLe64(header + 20);
    for (uint32_t i = 0; i < entries; ++i)
    {
        uint8_t entry[16];
        if (!m_Reader.ReadAt(offset + 32 + int64_t(i) * stride, entry, stride))
            return false;
        if (super)
        {
            const uint64_t child = avi::ReadLe64(entry);
            if (!child && !avi::ReadLe32(entry + 8))
                continue;
            if (child > uint64_t(std::numeric_limits<int64_t>::max()) ||
                !ParsePaletteIndex(int64_t(child), stream, changes, seen, count, used, false))
                return false;
        }
        else
        {
            const uint32_t relative = avi::ReadLe32(entry);
            if (base > uint64_t(std::numeric_limits<int64_t>::max()) - relative)
                return false;
            const int64_t position = int64_t(base + relative);
            size_t low = 0, high = count;
            while (low < high)
            {
                const size_t middle = low + (high - low) / 2;
                if (changes[middle]->offset < position)
                    low = middle + 1;
                else
                    high = middle;
            }
            if (low == count || changes[low]->offset != position || seen[low] ||
                changes[low]->size != (avi::ReadLe32(entry + 4) & 0x7fffffffu))
                return false;
            seen[low] = 1;
            ++used;
        }
    }
    return true;
}

bool AviDemuxer::ScanMovieLists(bool palettes)
{
    if (!m_Reader.SeekTo(0))
        return false;
    while (m_Reader.Tell() < m_Reader.FileSize())
    {
        RiffReader::Chunk root;
        if (!m_Reader.ReadChunkHeader(root))
            return false;
        if (root.id == avi::kFourCC_RIFF &&
            (root.listType == avi::kFourCC_AVI || root.listType == avi::kFourCC_AVIX))
        {
            if (!m_Reader.DescendInto(root))
                return false;
            const int64_t end = root.dataOffset - 4 + root.size;
            while (m_Reader.Tell() < end)
            {
                RiffReader::Chunk child;
                if (!m_Reader.ReadChunkHeader(child))
                    return false;
                if (child.IsList() && child.listType == avi::kFourCC_movi &&
                    !(palettes ? ScanPaletteList(child) : ScanMoviList(child)))
                    return false;
                if (!m_Reader.SkipChunk(child))
                    return false;
            }
            if (!m_Reader.Ascend())
                return false;
        }
        if (!m_Reader.SkipChunk(root))
            return false;
    }
    return true;
}

bool AviDemuxer::BuildMoviIndex()
{
    for (size_t i = 0; i < m_Streams.size(); ++i)
        m_Streams[i].index.clear();

    if (!ScanMovieLists(false))
        return false;

    return true;
}

bool AviDemuxer::ScanMoviList(const RiffReader::Chunk &list)
{
    if (!m_Reader.DescendInto(list))
        return false;

    const int64_t end = list.dataOffset - 4 + list.size;
    while (m_Reader.Tell() < end)
    {
        RiffReader::Chunk child;
        if (!m_Reader.ReadChunkHeader(child))
            return false;
        if (child.IsList() && child.listType == avi::kFourCC_rec)
        {
            if (!ScanMoviList(child))
                return false;
        }
        else
        {
            const int tens = (child.id & 255) - '0';
            const int ones = ((child.id >> 8) & 255) - '0';
            const int streamIndex = tens * 10 + ones;
            if (tens >= 0 && tens <= 9 && ones >= 0 && ones <= 9 &&
                streamIndex < static_cast<int>(m_Streams.size()))
            {
                StreamEntry &stream = m_Streams[streamIndex];
                const uint32_t kind = child.id >> 16;
                const bool video = stream.info.type == avi::kStreamType_vids &&
                    (kind == ('d' | ('b' << 8)) || kind == ('d' | ('c' << 8)));
                const bool audio = stream.info.type == avi::kStreamType_auds && kind == ('w' | ('b' << 8));
                if (video || audio)
                {
                    if (child.size > kMaxFramePayloadBytes || stream.index.size() >= kMaxFramesPerStream)
                        return false;
                    avi::FrameIndexEntry frame;
                    frame.offset = child.dataOffset;
                    frame.size = child.size;
                    frame.isKeyframe = kind == ('d' | ('b' << 8));
                    stream.index.push_back(frame);
                }
            }
        }
        if (!m_Reader.SkipChunk(child))
            return false;
    }
    return m_Reader.Ascend();
}

// ---------------------------------------------------------------------------
// Classic idx1 index
// ---------------------------------------------------------------------------

bool AviDemuxer::BuildIdx1Index()
{
    if (m_Idx1Offset <= 0 || m_Idx1Size < sizeof(avi::AviOldIndexEntry) ||
        m_Idx1Size % sizeof(avi::AviOldIndexEntry) != 0)
        return false;
    if (m_Idx1Offset > m_Reader.FileSize())
        return false;
    if (static_cast<uint64_t>(m_Idx1Size) >
        static_cast<uint64_t>(m_Reader.FileSize() - m_Idx1Offset))
        return false;

    const uint32_t entryCount = m_Idx1Size / sizeof(avi::AviOldIndexEntry);
    if (entryCount == 0)
        return false;
    if (entryCount > kMaxIdx1Entries)
        return false;

    size_t idx1Bytes = 0;
    if (MulSizeOverflow(static_cast<size_t>(entryCount), sizeof(avi::AviOldIndexEntry), idx1Bytes))
        return false;

    std::vector<avi::AviOldIndexEntry> idx1Entries;
    try
    {
        idx1Entries.resize(entryCount);
    }
    catch (...)
    {
        return false;
    }

    if (!m_Reader.SeekTo(m_Idx1Offset))
        return false;
    if (!m_Reader.Read(idx1Entries.data(), idx1Bytes))
        return false;

    for (auto &stream : m_Streams)
        stream.index.clear();

    // Build per-stream chunk id -> stream index lookup.
    // Pre-compute expected chunk ids for each stream.
    std::vector<ChunkIdMap> idMap;
    for (int i = 0; i < static_cast<int>(m_Streams.size()); ++i)
    {
        // Both compressed ('dc'/'wb') and uncompressed ('db') variants.
        const uint32_t compId = avi::StreamChunkId(i, m_Streams[i].info.type);
        idMap.push_back({compId, i});

        // 'db' (uncompressed video) variant
        if (m_Streams[i].info.type == avi::kStreamType_vids)
        {
            const uint32_t rawId = avi::MakeFourCC(
                static_cast<char>('0' + (i / 10) % 10),
                static_cast<char>('0' + i % 10),
                'd', 'b');
            idMap.push_back({rawId, i});
        }
    }

    // Spec allows idx1 offsets to be absolute or relative to the first byte
    // of the 'movi' identifier.
    const int64_t moviIdOffset = (m_MoviDataOffset >= 4) ? (m_MoviDataOffset - 4) : 0;
    int64_t offsetBase = 0;
    bool haveMoviBase = moviIdOffset > 0;
    const int64_t fileSize = m_Reader.FileSize();

    int absScore = 0;
    int moviScore = 0;
    int sampled = 0;
    const int kMaxSamples = 128;

    for (uint32_t n = 0; n < entryCount; ++n)
    {
        if (sampled >= kMaxSamples)
            break;
        const avi::AviOldIndexEntry &e = idx1Entries[n];
        if (e.dwSize == 0)
            continue;
        if (e.dwSize > kMaxFramePayloadBytes)
            continue;
        if (FindStreamIndexByChunkId(idMap, e.dwChunkId) < 0)
            continue;

        if (IsIdx1EntryOffsetHeaderValid(m_Reader, e, 0, fileSize))
            ++absScore;
        if (haveMoviBase && IsIdx1EntryOffsetHeaderValid(m_Reader, e, moviIdOffset, fileSize))
            ++moviScore;
        ++sampled;
    }

    if (moviScore > absScore)
        offsetBase = moviIdOffset;

    bool explicitPalette[kMaxStreams] = {};
    for (uint32_t n = 0; n < entryCount; ++n)
    {
        const uint32_t id = idx1Entries[n].dwChunkId;
        const int stream = avi::StreamIndexFromChunkId(id);
        if ((id >> 16) == ('p' | ('c' << 8)) && stream >= 0 &&
            stream < int(m_Streams.size()) && m_Streams[stream].info.type == avi::kStreamType_vids &&
            m_Streams[stream].info.bitsPerPixel > 0 && m_Streams[stream].info.bitsPerPixel <= 8)
            explicitPalette[stream] = true;
    }
    for (uint32_t n = 0; n < entryCount; ++n)
    {
        const avi::AviOldIndexEntry &e = idx1Entries[n];

        const int paletteStream = avi::StreamIndexFromChunkId(e.dwChunkId);
        if ((e.dwChunkId >> 16) == ('p' | ('c' << 8)) && paletteStream >= 0 &&
            paletteStream < int(m_Streams.size()) && explicitPalette[paletteStream])
        {
            int64_t base = 0, dataOffset = 0, headerOffset = 0;
            const PaletteChange *change = nullptr;
            if (ResolveIdx1EntryBaseByHeader(m_Reader, e, offsetBase, haveMoviBase, moviIdOffset, fileSize, base) &&
                ComputeIdx1Offsets(e, base, fileSize, dataOffset, headerOffset))
                for (change = m_PaletteChanges; change; change = change->next)
                    if (change->offset == dataOffset && change->stream == paletteStream)
                        break;
            PaletteStep *step = new PaletteStep;
            step->next = nullptr;
            step->change = change && e.dwSize >= 4 + change->count * 4 ? change : nullptr;
            step->sequence = n;
            step->stream = paletteStream;
            if (m_LastPaletteStep)
                m_LastPaletteStep->next = step;
            else
                m_PaletteSteps = step;
            m_LastPaletteStep = step;
            continue;
        }
        if (e.dwSize > kMaxFramePayloadBytes)
            continue;

        // Find stream index.
        const int streamIdx = FindStreamIndexByChunkId(idMap, e.dwChunkId);
        if (streamIdx < 0)
            continue;

        const bool virtualDrop = !e.dwSize && !e.dwOffset &&
            m_Streams[streamIdx].info.type == avi::kStreamType_vids;
        int64_t resolvedBase = 0;
        if (!virtualDrop && !ResolveIdx1EntryBaseByHeader(m_Reader, e, offsetBase, haveMoviBase, moviIdOffset,
                                                         fileSize, resolvedBase))
            continue;

        if (m_Streams[streamIdx].index.size() >= kMaxFramesPerStream)
            return false;

        avi::FrameIndexEntry fe;
        // The offset in idx1 points to the chunk header (8 bytes before data).
        if (virtualDrop)
            fe.offset = 0;
        else if (AddI64Overflow(resolvedBase, static_cast<int64_t>(e.dwOffset) + 8, fe.offset))
            continue;
        fe.paletteStep = explicitPalette[streamIdx] ? int(n) : -1;
        fe.size = e.dwSize;
        fe.isKeyframe = (e.dwFlags & avi::kAVIIF_KEYFRAME) != 0;
        try
        {
            m_Streams[streamIdx].index.push_back(fe);
        }
        catch (...)
        {
            return false;
        }
    }

    // Verify at least one stream got some frames.
    for (const auto &s : m_Streams)
    {
        if (!s.index.empty())
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// OpenDML index
// ---------------------------------------------------------------------------

bool AviDemuxer::BuildOpenDmlIndex()
{
    bool anySuccess = false;

    for (auto &stream : m_Streams)
    {
        stream.index.clear();

        if (!stream.headerIndex.empty())
            stream.index = stream.headerIndex;

        for (const auto &superEntry : stream.superIndex)
        {
            if (superEntry.qwOffset == 0 || superEntry.dwSize == 0)
                continue;

            if (!ParseStandardIndex(static_cast<int64_t>(superEntry.qwOffset),
                                    superEntry.dwSize, stream.superIndexChunkId, stream.index))
            {
                // A successful prefix is not a complete stream timeline.
                stream.index.clear();
                break;
            }
        }

        if (!stream.index.empty())
            anySuccess = true;
        if (stream.index.size() > kMaxFramesPerStream)
            return false;
    }

    return anySuccess;
}

bool AviDemuxer::ParseStandardIndex(int64_t offset, uint32_t chunkSize, uint32_t expectedChunkId,
                                     std::vector<avi::FrameIndexEntry> &outEntries)
{
    if (offset < 0 || offset > m_Reader.FileSize())
        return false;
    if (chunkSize != 0 && chunkSize < sizeof(avi::AviStdIndexHeader))
        return false;

    uint8_t chunkHdr[8];
    if (!m_Reader.ReadAt(offset, chunkHdr, sizeof(chunkHdr)))
        return false;

    const uint32_t chunkId        = avi::ReadLe32(chunkHdr + 0);
    const uint32_t onDiskDataSize = avi::ReadLe32(chunkHdr + 4);
    const bool isIxChunk = ((chunkId & 0x0000FFFFu) == avi::MakeFourCC('i', 'x', 0, 0));
    if (!isIxChunk && chunkId != avi::kFourCC_indx)
        return false;
    if (onDiskDataSize < sizeof(avi::AviStdIndexHeader))
        return false;

    const uint64_t onDiskTotalSize = static_cast<uint64_t>(onDiskDataSize) + 8u;
    const uint64_t fileRemaining = static_cast<uint64_t>(m_Reader.FileSize() - offset);
    if (onDiskTotalSize > fileRemaining)
        return false;

    // Some writers store super-index dwSize as either RIFF cb or total chunk size.
    // Treat the on-disk chunk header as authoritative and use dwSize as advisory.
    const uint64_t advisorySize = static_cast<uint64_t>(chunkSize);
    const uint64_t onDiskPaddedTotal = onDiskTotalSize + (onDiskDataSize & 1u);
    if (advisorySize != 0 &&
        advisorySize != static_cast<uint64_t>(onDiskDataSize) &&
        advisorySize != onDiskTotalSize &&
        advisorySize != onDiskPaddedTotal)
    {
        // Keep parsing; advisory size mismatch is tolerated for compatibility.
    }

    const uint64_t boundedChunkSize = onDiskTotalSize;
    if (boundedChunkSize < 8u + sizeof(avi::AviStdIndexHeader))
        return false;

    const int64_t headerStart = offset + 8; // skip chunk id + size
    avi::AviStdIndexHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    if (!m_Reader.ReadAt(headerStart, &hdr, sizeof(hdr)))
        return false;

    if (hdr.bIndexType != avi::kAVI_INDEX_OF_CHUNKS)
        return false;
    const bool fields = hdr.bIndexSubType == avi::kAVI_INDEX_2FIELD;
    if ((!fields && hdr.bIndexSubType != 0) || hdr.wLongsPerEntry != (fields ? 3 : 2))
        return false;
    if (expectedChunkId != 0 && hdr.dwChunkId != expectedChunkId)
        return false;

    const uint32_t count = hdr.nEntriesInUse;
    if (count > kMaxStdIndexEntries)
        return false;
    if (count == 0)
        return true;

    size_t entryBytes = 0;
    const size_t entrySize = fields ? 12u : sizeof(avi::AviStdIndexEntry);
    if (MulSizeOverflow(static_cast<size_t>(count), entrySize, entryBytes))
        return false;
    if (entryBytes > (boundedChunkSize - 8u - sizeof(avi::AviStdIndexHeader)))
        return false;
    if (outEntries.size() >= kMaxFramesPerStream)
        return false;
    if (count > (kMaxFramesPerStream - outEntries.size()))
        return false;

    const int64_t entriesOffset = headerStart + sizeof(avi::AviStdIndexHeader);
    if (!m_Reader.SeekTo(entriesOffset))
        return false;

    const int64_t baseOffset = static_cast<int64_t>(hdr.qwBaseOffset);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint8_t entry[12];
        if (!m_Reader.Read(entry, entrySize))
            return false;
        avi::AviStdIndexEntry e;
        e.dwOffset = avi::ReadLe32(entry);
        e.dwSize = avi::ReadLe32(entry + 4);

        const uint32_t frameSize = e.dwSize & 0x7FFFFFFFu;
        if (frameSize > kMaxFramePayloadBytes)
            return false;

        avi::FrameIndexEntry fe;
        if (AddI64Overflow(baseOffset, static_cast<int64_t>(e.dwOffset), fe.offset))
            return false;
        fe.size = frameSize; // mask off not-keyframe flag
        fe.isKeyframe = (e.dwSize & 0x80000000u) == 0; // bit 31 set = NOT keyframe

        if (fe.offset < 0)
            return false;
        int64_t frameEnd = 0;
        if (AddI64Overflow(fe.offset, static_cast<int64_t>(fe.size), frameEnd))
            return false;
        if (frameEnd > m_Reader.FileSize())
            return false;

        // Both offsets are relative to qwBaseOffset. dwSize covers the whole
        // frame, not just field one; the decoder consumes both JPEG fields.
        if (fields && avi::ReadLe32(entry + 8) != 0)
        {
            int64_t secondField = 0;
            if (AddI64Overflow(baseOffset, avi::ReadLe32(entry + 8), secondField) ||
                secondField <= fe.offset || secondField >= frameEnd)
                return false;
        }

        try
        {
            outEntries.push_back(fe);
        }
        catch (...)
        {
            return false;
        }
    }

    return true;
}
