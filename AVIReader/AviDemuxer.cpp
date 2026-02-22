#include "AviDemuxer.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace
{

constexpr uint32_t kMaxStreams = 64;
constexpr uint32_t kMaxFramePayloadBytes = 256u * 1024u * 1024u; // 256 MiB
constexpr uint32_t kMaxFormatChunkBytes = 4u * 1024u * 1024u;    // 4 MiB
constexpr uint32_t kMaxIdx1Entries = 4000000u;
constexpr uint32_t kMaxSuperIndexEntries = 262144u;
constexpr uint32_t kMaxStdIndexEntries = 1000000u;
constexpr size_t kMaxFramesPerStream = 4000000u;

bool MulSizeOverflow(size_t a, size_t b, size_t &out)
{
    if (a != 0 && b > (std::numeric_limits<size_t>::max() / a))
        return true;
    out = a * b;
    return false;
}

bool AddI64Overflow(int64_t a, int64_t b, int64_t &out)
{
    if ((b > 0 && a > (std::numeric_limits<int64_t>::max() - b)) ||
        (b < 0 && a < (std::numeric_limits<int64_t>::min() - b)))
        return true;
    out = a + b;
    return false;
}

} // namespace

// ===========================================================================
// Construction / destruction
// ===========================================================================

AviDemuxer::AviDemuxer()
    : m_Open(false),
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

        if (!BuildIndex())
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
    m_Reader.Close();
    m_Open = false;
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
    if (!entry || entry->size == 0 || entry->size > kMaxFramePayloadBytes)
    {
        outData.clear();
        return false;
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

    // Also scan for RIFF AVIX segments (OpenDML extended AVIs).
    // These contain additional 'movi' lists but we only need the super-index
    // (already captured in strl parsing) to locate all frames.
    // We don't need to explicitly parse them here since BuildOpenDmlIndex
    // follows the super-index pointers directly.

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
                // Prefer biCompression over fccHandler for the codec.
                if (bih.biCompression != 0)
                    stream.info.codec = bih.biCompression;
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
        else if (child.id == avi::kFourCC_indx && foundStrh)
        {
            // OpenDML super-index for this stream.
            ParseSuperIndex(child, stream);
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
        stream.info.alphaMask = 0xFF000000u;
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

bool AviDemuxer::ParseAudioFormat(const RiffReader::Chunk &chunk, avi::WaveFormatHeader &wfh)
{
    if (chunk.size < 16)
        return false;
    const size_t toRead = std::min(static_cast<size_t>(chunk.size),
                                   sizeof(avi::WaveFormatHeader));
    memset(&wfh, 0, sizeof(wfh));
    return m_Reader.Read(&wfh, toRead);
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

bool AviDemuxer::BuildIndex()
{
    // Prefer OpenDML super-index if any stream has one.
    bool hasOpenDml = false;
    for (const auto &s : m_Streams)
    {
        if (!s.superIndex.empty())
        {
            hasOpenDml = true;
            break;
        }
    }

    if (hasOpenDml)
    {
        if (BuildOpenDmlIndex())
            return true;
        // Fall through to idx1 if OpenDML parsing fails.
    }

    return BuildIdx1Index();
}

// ---------------------------------------------------------------------------
// Classic idx1 index
// ---------------------------------------------------------------------------

bool AviDemuxer::BuildIdx1Index()
{
    if (m_Idx1Offset <= 0 || m_Idx1Size < sizeof(avi::AviOldIndexEntry))
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

    for (auto &stream : m_Streams)
        stream.index.clear();

    // Build per-stream chunk id -> stream index lookup.
    // Pre-compute expected chunk ids for each stream.
    struct ChunkIdMap { uint32_t chunkId; int streamIdx; };
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

    auto findStreamIndexByChunkId = [&idMap](uint32_t chunkId) -> int
    {
        for (const auto &m : idMap)
        {
            if (m.chunkId == chunkId)
                return m.streamIdx;
        }
        return -1;
    };

    auto readIdx1Entry = [this](uint32_t index, avi::AviOldIndexEntry &e) -> bool
    {
        int64_t entryOffset = 0;
        if (AddI64Overflow(m_Idx1Offset,
                           static_cast<int64_t>(index) * static_cast<int64_t>(sizeof(avi::AviOldIndexEntry)),
                           entryOffset))
            return false;
        return m_Reader.ReadAt(entryOffset, &e, sizeof(e));
    };

    auto isEntryOffsetValid = [this](const avi::AviOldIndexEntry &e, int64_t base) -> bool
    {
        const int64_t fileSize = m_Reader.FileSize();
        if (base < 0 || base > fileSize)
            return false;

        int64_t dataOffset = 0;
        if (AddI64Overflow(base, static_cast<int64_t>(e.dwOffset) + 8, dataOffset))
            return false;
        const int64_t headerOffset = dataOffset - 8;

        if (headerOffset < 0 || dataOffset < 0)
            return false;
        if (headerOffset + 8 > fileSize)
            return false;
        if (dataOffset + static_cast<int64_t>(e.dwSize) > fileSize)
            return false;

        uint8_t hdr[8];
        if (!m_Reader.ReadAt(headerOffset, hdr, sizeof(hdr)))
            return false;

        const uint32_t chunkId   = avi::ReadLe32(hdr + 0);
        const uint32_t chunkSize = avi::ReadLe32(hdr + 4);
        if (chunkId != e.dwChunkId)
            return false;
        if (chunkSize < e.dwSize)
            return false;

        return true;
    };

    // Spec allows idx1 offsets to be absolute or relative to the first byte
    // of the 'movi' identifier.
    const int64_t moviIdOffset = (m_MoviDataOffset >= 4) ? (m_MoviDataOffset - 4) : 0;
    int64_t offsetBase = 0;
    bool haveMoviBase = moviIdOffset > 0;

    int absScore = 0;
    int moviScore = 0;
    int sampled = 0;
    const int kMaxSamples = 128;

    for (uint32_t n = 0; n < entryCount; ++n)
    {
        avi::AviOldIndexEntry e;
        if (!readIdx1Entry(n, e))
            return false;

        if (sampled >= kMaxSamples)
            continue;
        if (e.dwSize == 0)
            continue;
        if (e.dwSize > kMaxFramePayloadBytes)
            continue;
        if (findStreamIndexByChunkId(e.dwChunkId) < 0)
            continue;

        if (isEntryOffsetValid(e, 0))
            ++absScore;
        if (haveMoviBase && isEntryOffsetValid(e, moviIdOffset))
            ++moviScore;
        ++sampled;
    }

    if (moviScore > absScore)
        offsetBase = moviIdOffset;

    for (uint32_t n = 0; n < entryCount; ++n)
    {
        avi::AviOldIndexEntry e;
        if (!readIdx1Entry(n, e))
            return false;

        if (e.dwSize == 0 || e.dwSize > kMaxFramePayloadBytes)
            continue;

        // Find stream index.
        const int streamIdx = findStreamIndexByChunkId(e.dwChunkId);
        if (streamIdx < 0)
            continue;

        int64_t resolvedBase = offsetBase;
        if (!isEntryOffsetValid(e, resolvedBase))
        {
            // Some damaged files mix both styles; try the alternate base per-entry.
            const int64_t altBase = (resolvedBase == 0 && haveMoviBase) ? moviIdOffset : 0;
            if (!isEntryOffsetValid(e, altBase))
                continue;
            resolvedBase = altBase;
        }
        if (m_Streams[streamIdx].index.size() >= kMaxFramesPerStream)
            return false;

        avi::FrameIndexEntry fe;
        // The offset in idx1 points to the chunk header (8 bytes before data).
        if (AddI64Overflow(resolvedBase, static_cast<int64_t>(e.dwOffset) + 8, fe.offset))
            continue;
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
        if (stream.superIndex.empty())
            continue;

        for (const auto &superEntry : stream.superIndex)
        {
            if (superEntry.qwOffset == 0 || superEntry.dwSize == 0)
                continue;

            if (!ParseStandardIndex(static_cast<int64_t>(superEntry.qwOffset),
                                    superEntry.dwSize, stream.superIndexChunkId, stream.index))
            {
                // Tolerate individual failures; some entries may point outside
                // the file if the writer crashed.
                continue;
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
    if (chunkSize < 8 + sizeof(avi::AviStdIndexHeader))
        return false;

    uint8_t chunkHdr[8];
    if (!m_Reader.ReadAt(offset, chunkHdr, sizeof(chunkHdr)))
        return false;

    const uint32_t chunkId        = avi::ReadLe32(chunkHdr + 0);
    const uint32_t onDiskDataSize = avi::ReadLe32(chunkHdr + 4);
    if ((chunkId & 0x0000FFFFu) != avi::MakeFourCC('i', 'x', 0, 0))
        return false;
    if (onDiskDataSize < sizeof(avi::AviStdIndexHeader))
        return false;

    const uint64_t onDiskTotalSize = static_cast<uint64_t>(onDiskDataSize) + 8u;
    const uint64_t fileRemaining = static_cast<uint64_t>(m_Reader.FileSize() - offset);
    if (onDiskTotalSize > fileRemaining)
        return false;

    uint64_t boundedChunkSize = std::min<uint64_t>(chunkSize, onDiskTotalSize);
    if (boundedChunkSize < 8u + sizeof(avi::AviStdIndexHeader))
        return false;

    const int64_t headerStart = offset + 8; // skip chunk id + size
    avi::AviStdIndexHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    if (!m_Reader.ReadAt(headerStart, &hdr, sizeof(hdr)))
        return false;

    if (hdr.bIndexType != avi::kAVI_INDEX_OF_CHUNKS)
        return false;
    if (hdr.wLongsPerEntry != 2)
        return false;
    if (expectedChunkId != 0 && hdr.dwChunkId != expectedChunkId)
        return false;

    const uint32_t count = hdr.nEntriesInUse;
    if (count > kMaxStdIndexEntries)
        return false;
    if (count == 0)
        return true;

    size_t entryBytes = 0;
    if (MulSizeOverflow(static_cast<size_t>(count), sizeof(avi::AviStdIndexEntry), entryBytes))
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
        avi::AviStdIndexEntry e;
        if (!m_Reader.Read(&e, sizeof(e)))
            return false;

        const uint32_t frameSize = e.dwSize & 0x7FFFFFFFu;
        if (frameSize == 0 || frameSize > kMaxFramePayloadBytes)
            continue;

        avi::FrameIndexEntry fe;
        if (AddI64Overflow(baseOffset, static_cast<int64_t>(e.dwOffset), fe.offset))
            continue;
        fe.size = frameSize; // mask off not-keyframe flag
        fe.isKeyframe = (e.dwSize & 0x80000000u) == 0; // bit 31 set = NOT keyframe

        if (fe.offset < 0)
            continue;
        int64_t frameEnd = 0;
        if (AddI64Overflow(fe.offset, static_cast<int64_t>(fe.size), frameEnd))
            continue;
        if (frameEnd > m_Reader.FileSize())
            continue;

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

