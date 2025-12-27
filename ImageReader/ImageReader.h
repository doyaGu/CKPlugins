#ifndef IMAGEREADER_H
#define IMAGEREADER_H

#include "CKBitmapReader.h"
#include "CKGlobals.h"
#include "VxColor.h"

#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <cstdint>

#define READER_INDEX_BMP 0
#define READER_INDEX_TGA 1
#define READER_INDEX_PCX 2
#define READER_COUNT 3
extern CKPluginInfo g_PluginInfo[READER_COUNT];

//=============================================================================
// Extended bitmap properties structures
//
// Virtools expects each reader to return a CKBitmapProperties* whose m_Size
// indicates the *actual* size of the reader-specific derived structure.
// The original ImageReader.dll uses per-format extensions (BMP/TGA/PCX).
//=============================================================================

// BMP extended properties: 76 bytes total
// Offset 72: m_BitDepth (8, 9=RLE8, 16, 24, 32)
struct BmpBitmapProperties : public CKBitmapProperties
{
    BmpBitmapProperties() { Init(CKGUID(), nullptr); }
    BmpBitmapProperties(const CKGUID &readerGuid, const char *ext) { Init(readerGuid, ext); }

    void Init(const CKGUID &readerGuid, const char *ext)
    {
        // Keep consistent with the original DLL: zero the whole struct, then set defaults.
        // (CKBitmapProperties is a plain data structure in Virtools SDK.)
        memset(this, 0, sizeof(*this));
        m_Size = sizeof(BmpBitmapProperties);
        m_ReaderGuid = readerGuid;
        if (ext)
            m_Ext = CKFileExtension((char *)ext);
        m_Format.Size = sizeof(VxImageDescEx);
        m_BitDepth = 24;
    }

    // Extended fields
    CKDWORD m_BitDepth; // 0x48 (offset 72): Bit depth for saving (default 24)
};

// TGA extended properties: 80 bytes total
// Offset 72: m_BitDepth (24 or 32)
// Offset 76: m_UseRLE (0 or 1)
struct TgaBitmapProperties : public CKBitmapProperties
{
    TgaBitmapProperties() { Init(CKGUID(), nullptr); }
    TgaBitmapProperties(const CKGUID &readerGuid, const char *ext) { Init(readerGuid, ext); }

    void Init(const CKGUID &readerGuid, const char *ext)
    {
        memset(this, 0, sizeof(*this));
        m_Size = sizeof(TgaBitmapProperties);
        m_ReaderGuid = readerGuid;
        if (ext)
            m_Ext = CKFileExtension((char *)ext);
        m_Format.Size = sizeof(VxImageDescEx);
        m_BitDepth = 24;
        m_UseRLE = 0;
    }

    // Extended fields
    CKDWORD m_BitDepth; // 0x48 (offset 72): Bit depth for saving (default 24)
    CKDWORD m_UseRLE;   // 0x4C (offset 76): Use RLE compression (default 0)
};

// PCX extended properties: 80 bytes total
// Offset 72: m_BitDepth (default 24, unused)
// Offset 76: m_UseRLE (default 0, unused)
struct PcxBitmapProperties : public CKBitmapProperties
{
    PcxBitmapProperties() { Init(CKGUID(), nullptr); }
    PcxBitmapProperties(const CKGUID &readerGuid, const char *ext) { Init(readerGuid, ext); }

    void Init(const CKGUID &readerGuid, const char *ext)
    {
        memset(this, 0, sizeof(*this));
        m_Size = sizeof(PcxBitmapProperties);
        m_ReaderGuid = readerGuid;
        if (ext)
            m_Ext = CKFileExtension((char *)ext);
        m_Format.Size = sizeof(VxImageDescEx);
        m_BitDepth = 24;
        m_UseRLE = 0;
    }

    // Extended fields (unused for PCX, but present for consistency)
    CKDWORD m_BitDepth; // 0x48 (offset 72): Reserved (default 24)
    CKDWORD m_UseRLE;   // 0x4C (offset 76): Reserved (default 0)
};

// Shared base class for BMP/TGA/PCX readers.
// Implements common CKBitmapReader glue and SDK-safe memory management.
// Subclasses must define their own extended m_Properties member.
class ImageReader : public CKBitmapReader
{
public:
    virtual ~ImageReader() {}

    // CKDataReader interface
    virtual void Release() { delete this; }

    // Common flags implementation - returns 15 (CK_DATAREADER_FLAGS)
    virtual CK_DATAREADER_FLAGS GetFlags() { return (CK_DATAREADER_FLAGS)15; }

    // CKBitmapReader interface - these should be overridden by concrete readers
    virtual int ReadFile(CKSTRING filename, CKBitmapProperties **bp)
    {
        if (bp)
            *bp = NULL;
        return 1;
    }

    virtual int ReadMemory(void *memory, int size, CKBitmapProperties **bp)
    {
        if (bp)
            *bp = NULL;
        return 1;
    }

    virtual int ReadASynchronousFile(CKSTRING /*filename*/, CKBitmapProperties **bp)
    {
        if (bp)
            *bp = NULL;
        return CKBITMAPERROR_UNSUPPORTEDFUNCTION;
    }

    virtual int SaveFile(CKSTRING filename, CKBitmapProperties *bp)
    {
        return 0;
    }

    virtual int SaveMemory(void **memory, CKBitmapProperties *bp)
    {
        return 0;
    }

    virtual CKBOOL IsAlphaSaved(CKBitmapProperties * /*bp*/) { return FALSE; }

    virtual void ReleaseMemory(void *memory) { delete[] static_cast<CKBYTE *>(memory); }

    // Default properties
    virtual void GetBitmapDefaultProperties(CKBitmapProperties **bp)
    {
        if (bp)
            *bp = NULL;
    }

    virtual void SetBitmapDefaultProperties(CKBitmapProperties * /*bp*/) {}

    // Shared helper to fill a VxImageDescEx for BGRA32 format
    static void FillFormatBGRA32(VxImageDescEx &fmt, int width, int height, int bytesPerLine, CKBYTE *image)
    {
        memset(&fmt, 0, sizeof(VxImageDescEx));
        fmt.Size = sizeof(VxImageDescEx);
        fmt.Flags = 0;
        fmt.Width = width;
        fmt.Height = height;
        fmt.BytesPerLine = bytesPerLine;
        fmt.BitsPerPixel = 32;
        fmt.RedMask = R_MASK;
        fmt.GreenMask = G_MASK;
        fmt.BlueMask = B_MASK;
        fmt.AlphaMask = A_MASK;
        fmt.BytesPerColorEntry = 0;
        fmt.ColorMapEntries = 0;
        fmt.ColorMap = NULL;
        fmt.Image = image;
    }

    // Free image data associated with properties.
    // Ownership rules (mirroring original behavior):
    // - If m_Data is non-null, it owns the allocation backing the image (and potentially other sub-pointers).
    // - Otherwise, m_Format.Image may own an allocation.
    static void FreeBitmapData(CKBitmapProperties *props)
    {
        if (!props)
            return;

        if (props->m_Data)
            delete[] static_cast<CKBYTE *>(props->m_Data);

        props->m_Data = NULL;
    }

protected:
    ImageReader() {}

private:
    ImageReader(const ImageReader &);
    ImageReader &operator=(const ImageReader &);
};

#endif // IMAGEREADER_H
