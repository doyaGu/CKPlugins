#ifndef RIFFREADER_H
#define RIFFREADER_H

#include "AviTypes.h"

#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// RiffReader -- low-level, portable RIFF chunk traversal over a file stream.
//
// Design:
//   - Provides a stack-based descent model (DescendInto / Ascend) so callers
//     can walk nested LIST/RIFF containers without manual offset arithmetic.
//   - All reads are little-endian; we byte-swap on big-endian hosts.
//   - Owns the FILE* lifetime (Open / Close).
// ---------------------------------------------------------------------------

class RiffReader
{
public:
    /// Represents one RIFF chunk in the file.
    struct Chunk
    {
        uint32_t id;         ///< FourCC chunk id (e.g. 'avih', 'idx1').
        uint32_t size;       ///< Data size (excluding 8-byte header, excluding pad).
        uint32_t listType;   ///< For RIFF/LIST: the form type (e.g. 'AVI ', 'hdrl'). Zero otherwise.
        int64_t  dataOffset; ///< Absolute file offset where chunk data begins.

        bool IsList() const { return id == avi::kFourCC_RIFF || id == avi::kFourCC_LIST; }
    };

    RiffReader();
    ~RiffReader();

    // Non-copyable
    RiffReader(const RiffReader &) = delete;
    RiffReader &operator=(const RiffReader &) = delete;

    /// Open a file for RIFF reading. Returns true on success.
    bool Open(const char *filename);

    /// Close the file and reset state.
    void Close();

    /// True if a file is currently open.
    bool IsOpen() const { return m_File != nullptr; }

    /// Total size of the open file in bytes.
    int64_t FileSize() const { return m_FileSize; }

    // ------------------------------------------------------------------
    // Chunk navigation
    // ------------------------------------------------------------------

    /// Read the next chunk header at the current file position.
    /// For LIST/RIFF chunks the listType is also read and dataOffset
    /// points past the 4-byte form type.
    bool ReadChunkHeader(Chunk &chunk);

    /// Descend into a LIST or RIFF chunk. Pushes the current level
    /// so that iteration is bounded to the container's data region.
    bool DescendInto(const Chunk &listChunk);

    /// Return to the parent container, restoring the previous bounds.
    bool Ascend();

    /// Advance past the current chunk to position at the start of the
    /// next sibling chunk (respecting the 2-byte RIFF pad alignment).
    bool SkipChunk(const Chunk &chunk);

    // ------------------------------------------------------------------
    // Data reading
    // ------------------------------------------------------------------

    /// Read `size` bytes from the current file position.
    bool Read(void *buffer, size_t size);

    /// Read `size` bytes from an absolute file offset (seeks first).
    bool ReadAt(int64_t offset, void *buffer, size_t size);

    /// Seek to an absolute file offset.
    bool SeekTo(int64_t offset);

    /// Current absolute file position.
    int64_t Tell() const;

    // ------------------------------------------------------------------
    // Convenience typed readers (little-endian)
    // ------------------------------------------------------------------

    bool ReadU16(uint16_t &v);
    bool ReadU32(uint32_t &v);
    bool ReadU64(uint64_t &v);
    bool ReadI32(int32_t &v);

private:
    /// One level in the descent stack, tracking the data bounds
    /// of the enclosing LIST/RIFF container.
    struct Level
    {
        int64_t dataStart; ///< First byte of container data.
        int64_t dataEnd;   ///< One past the last byte of container data.
    };

    FILE              *m_File;
    int64_t            m_FileSize;
    std::vector<Level> m_Stack;

    /// Portable 64-bit seek.
    static bool FileSeek(FILE *f, int64_t offset);
    /// Portable 64-bit tell.
    static int64_t FileTell(FILE *f);
    /// Get file size via seek-to-end.
    static int64_t FileGetSize(FILE *f);

    /// Returns the end-of-data boundary of the current enclosing level,
    /// or the file size if at the top level.
    int64_t CurrentBound() const;
};

#endif // RIFFREADER_H
