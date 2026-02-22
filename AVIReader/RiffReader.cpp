#include "RiffReader.h"

#include <algorithm>
#include <cstring>
#include <limits>

// ---------------------------------------------------------------------------
// Portable 64-bit file operations
// ---------------------------------------------------------------------------

#if defined(_MSC_VER) || defined(_WIN32)
#  define RIFF_FSEEK(f, off) (_fseeki64((f), (off), SEEK_SET) == 0)
#  define RIFF_FTELL(f)      _ftelli64(f)
#else
#  define RIFF_FSEEK(f, off) (fseeko((f), static_cast<off_t>(off), SEEK_SET) == 0)
#  define RIFF_FTELL(f)      static_cast<int64_t>(ftello(f))
#endif

bool RiffReader::FileSeek(FILE *f, int64_t offset)
{
    return RIFF_FSEEK(f, offset);
}

int64_t RiffReader::FileTell(FILE *f)
{
    return RIFF_FTELL(f);
}

int64_t RiffReader::FileGetSize(FILE *f)
{
    if (!f)
        return -1;
    const int64_t saved = FileTell(f);
    if (saved < 0)
        return -1;
#if defined(_MSC_VER) || defined(_WIN32)
    if (_fseeki64(f, 0, SEEK_END) != 0)
        return -1;
#else
    if (fseeko(f, 0, SEEK_END) != 0)
        return -1;
#endif
    const int64_t size = FileTell(f);
    FileSeek(f, saved);
    return size;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

RiffReader::RiffReader()
    : m_File(nullptr), m_FileSize(0)
{}

RiffReader::~RiffReader()
{
    Close();
}

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

bool RiffReader::Open(const char *filename)
{
    Close();
    if (!filename || !filename[0])
        return false;

#if defined(_MSC_VER) || defined(_WIN32)
    // Use fopen_s on MSVC for safety; open in binary mode.
    if (fopen_s(&m_File, filename, "rb") != 0)
        m_File = nullptr;
#else
    m_File = fopen(filename, "rb");
#endif

    if (!m_File)
        return false;

    m_FileSize = FileGetSize(m_File);
    if (m_FileSize < 12) // minimum RIFF header
    {
        Close();
        return false;
    }

    // Rewind to start for the caller.
    FileSeek(m_File, 0);
    return true;
}

void RiffReader::Close()
{
    if (m_File)
    {
        fclose(m_File);
        m_File = nullptr;
    }
    m_FileSize = 0;
    m_Stack.clear();
}

// ---------------------------------------------------------------------------
// Current bound helper
// ---------------------------------------------------------------------------

int64_t RiffReader::CurrentBound() const
{
    if (m_Stack.empty())
        return m_FileSize;
    return m_Stack.back().dataEnd;
}

// ---------------------------------------------------------------------------
// Chunk navigation
// ---------------------------------------------------------------------------

bool RiffReader::ReadChunkHeader(Chunk &chunk)
{
    if (!m_File)
        return false;

    const int64_t pos = FileTell(m_File);
    if (pos < 0)
        return false;

    // Ensure at least 8 bytes remain within current bounds.
    const int64_t bound = CurrentBound();
    if (pos + 8 > bound)
        return false;

    // Read id (4 bytes) + size (4 bytes).
    uint8_t hdr[8];
    if (fread(hdr, 1, 8, m_File) != 8)
        return false;

    chunk.id   = avi::ReadLe32(hdr);
    chunk.size = avi::ReadLe32(hdr + 4);

    chunk.listType = 0;
    chunk.dataOffset = pos + 8;

    // For RIFF/LIST, read the 4-byte form type and adjust dataOffset.
    if (chunk.id == avi::kFourCC_RIFF || chunk.id == avi::kFourCC_LIST)
    {
        if (chunk.size < 4)
            return false;
        if (pos + 12 > bound)
            return false;

        uint8_t ft[4];
        if (fread(ft, 1, 4, m_File) != 4)
            return false;

        chunk.listType = avi::ReadLe32(ft);

        chunk.dataOffset = pos + 12; // past id + size + form type
    }

    const uint64_t chunkEnd = static_cast<uint64_t>(pos) + 8u + static_cast<uint64_t>(chunk.size);
    if (chunkEnd > static_cast<uint64_t>(bound))
        return false;

    return true;
}

bool RiffReader::DescendInto(const Chunk &listChunk)
{
    if (!listChunk.IsList())
        return false;
    if (listChunk.dataOffset < 4)
        return false;

    Level level;
    level.dataStart = listChunk.dataOffset;
    // The chunk.size includes the 4-byte form type, so total data region
    // starts after the 8-byte chunk header.
    const uint64_t listEnd =
        static_cast<uint64_t>(listChunk.dataOffset - 4) + static_cast<uint64_t>(listChunk.size);
    if (listEnd > static_cast<uint64_t>(CurrentBound()))
        return false;
    level.dataEnd = static_cast<int64_t>(listEnd);

    m_Stack.push_back(level);

    // Seek to the start of the first child chunk.
    return FileSeek(m_File, listChunk.dataOffset);
}

bool RiffReader::Ascend()
{
    if (m_Stack.empty())
        return false;

    const Level &level = m_Stack.back();
    // Seek to end of the container so the caller can continue with its siblings.
    if (!FileSeek(m_File, level.dataEnd))
        return false;
    m_Stack.pop_back();
    return true;
}

bool RiffReader::SkipChunk(const Chunk &chunk)
{
    // The next chunk starts after the 8-byte header + size (+ 1 pad byte if size is odd).
    if (chunk.dataOffset < (chunk.IsList() ? 4 : 0))
        return false;
    const uint64_t start = static_cast<uint64_t>(chunk.dataOffset - (chunk.IsList() ? 4 : 0));
    uint64_t next = start + static_cast<uint64_t>(chunk.size);
    // RIFF chunks are padded to 2-byte boundaries.
    if (chunk.size & 1)
        ++next;
    if (next > static_cast<uint64_t>(CurrentBound()))
        return false;
    return FileSeek(m_File, static_cast<int64_t>(next));
}

// ---------------------------------------------------------------------------
// Data reading
// ---------------------------------------------------------------------------

bool RiffReader::Read(void *buffer, size_t size)
{
    if (!m_File)
        return false;
    if (size == 0)
        return true;
    if (!buffer)
        return false;
    if (size > static_cast<size_t>(std::numeric_limits<int64_t>::max()))
        return false;

    const int64_t pos = Tell();
    if (pos < 0)
        return false;
    const int64_t end = pos + static_cast<int64_t>(size);
    if (end < pos || end > CurrentBound() || end > m_FileSize)
        return false;

    return fread(buffer, 1, size, m_File) == size;
}

bool RiffReader::ReadAt(int64_t offset, void *buffer, size_t size)
{
    if (size == 0)
        return SeekTo(offset);
    if (offset < 0 || offset > m_FileSize)
        return false;
    if (size > static_cast<size_t>(std::numeric_limits<int64_t>::max()))
        return false;
    if (offset + static_cast<int64_t>(size) < offset)
        return false;
    if (offset + static_cast<int64_t>(size) > m_FileSize)
        return false;
    if (!SeekTo(offset))
        return false;
    return Read(buffer, size);
}

bool RiffReader::SeekTo(int64_t offset)
{
    if (!m_File)
        return false;
    if (offset < 0 || offset > m_FileSize)
        return false;
    return FileSeek(m_File, offset);
}

int64_t RiffReader::Tell() const
{
    if (!m_File)
        return -1;
    return FileTell(m_File);
}

// ---------------------------------------------------------------------------
// Typed readers (little-endian)
// ---------------------------------------------------------------------------

bool RiffReader::ReadU16(uint16_t &v)
{
    uint8_t b[2];
    if (!Read(b, 2))
        return false;
    v = avi::ReadLe16(b);
    return true;
}

bool RiffReader::ReadU32(uint32_t &v)
{
    uint8_t b[4];
    if (!Read(b, 4))
        return false;
    v = avi::ReadLe32(b);
    return true;
}

bool RiffReader::ReadU64(uint64_t &v)
{
    uint8_t b[8];
    if (!Read(b, 8))
        return false;
    v = avi::ReadLe64(b);
    return true;
}

bool RiffReader::ReadI32(int32_t &v)
{
    uint32_t u;
    if (!ReadU32(u))
        return false;
    v = static_cast<int32_t>(u);
    return true;
}
