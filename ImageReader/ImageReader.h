#ifndef IMAGEREADER_H
#define IMAGEREADER_H

#include "CKBitmapReader.h"
#include "CKGlobals.h"
#include "VxColor.h"
#include "VxMath.h"
#include "XArray.h"

#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <climits>

#ifdef CK_LIB
#define g_PluginInfo g_ImageReader_PluginInfo
#endif

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
    BmpBitmapProperties(const CKGUID &readerGuid, CKSTRING ext) { Init(readerGuid, ext); }

    void Init(const CKGUID &readerGuid, CKSTRING ext)
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
// Offset 72: m_BitDepth (16, 24, 32, or 64=greyscale)
// Offset 76: m_UseRLE (0 or 1)
struct TgaBitmapProperties : public CKBitmapProperties
{
    TgaBitmapProperties() { Init(CKGUID(), nullptr); }
    TgaBitmapProperties(const CKGUID &readerGuid, CKSTRING ext) { Init(readerGuid, ext); }

    void Init(const CKGUID &readerGuid, CKSTRING ext)
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
    PcxBitmapProperties(const CKGUID &readerGuid, CKSTRING ext) { Init(readerGuid, ext); }

    void Init(const CKGUID &readerGuid, CKSTRING ext)
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

    // Pixels to save. CK2 save paths built from GetImageDesc() leave
    // m_Format.Image null and pass the surface through m_Data instead.
    static CKBYTE *GetSavePixels(CKBitmapProperties *bp)
    {
        if (bp->m_Format.Image)
            return bp->m_Format.Image;
        return static_cast<CKBYTE *>(bp->m_Data);
    }

    // Writers use BGRA32 internally. Convert through the source descriptor,
    // keeping both its pixels and palette borrowed and its fields unchanged.
    static CKBYTE *GetSavePixels(CKBitmapProperties *bp, XArray<CKBYTE> &converted, CKDWORD &stride)
    {
        VxImageDescEx source = bp->m_Format;
        source.Image = GetSavePixels(bp);
        if (!source.Image || source.Width <= 0 || source.Height <= 0 || source.Flags != 0)
            return NULL;
        if (source.BitsPerPixel != 8 && source.BitsPerPixel != 16 &&
            source.BitsPerPixel != 24 && source.BitsPerPixel != 32)
            return NULL;

        const uint64_t rowBytes = (uint64_t)source.Width * 4;
        const int64_t pitch = source.BytesPerLine;
        if (rowBytes > INT_MAX || (uint64_t)(pitch < 0 ? -pitch : pitch) <
            (uint64_t)source.Width * (source.BitsPerPixel / 8))
            return NULL;
        if (source.BitsPerPixel == 32 && source.ColorMapEntries == 0 &&
            source.RedMask == R_MASK && source.GreenMask == G_MASK &&
            source.BlueMask == B_MASK && source.AlphaMask == A_MASK && pitch > 0)
        {
            stride = (CKDWORD)pitch;
            return source.Image;
        }

        const uint64_t size = rowBytes * source.Height;
        if (size > INT_MAX) // XArray uses an int element count.
            return NULL;
        converted.Resize((int)size);
        VxImageDescEx target;
        FillFormatBGRA32(target, source.Width, source.Height, (int)rowBytes, converted.Begin());
        VxDoBlit(source, target);
        stride = (CKDWORD)rowBytes;
        return target.Image;
    }

    // Shared save-side pixel conversions.
    static CKBYTE ToGray(CKBYTE b, CKBYTE g, CKBYTE r)
    {
        return (CKBYTE)((r * 299 + g * 587 + b * 114) / 1000);
    }

    static CKWORD PackRGB555(CKBYTE b, CKBYTE g, CKBYTE r)
    {
        return (CKWORD)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    }

    // m_Data owns the decoded allocation. Image may alias it or be a separate
    // borrowed view; neither that separate view nor ColorMap is owned here.
    static void FreeBitmapData(CKBitmapProperties *props)
    {
        if (!props)
            return;

        if (props->m_Data) {
            if (props->m_Format.Image == props->m_Data)
                props->m_Format.Image = NULL;
            delete[] static_cast<CKBYTE *>(props->m_Data);
        }

        props->m_Data = NULL;
    }

protected:
    ImageReader() {}

private:
    ImageReader(const ImageReader &);
    ImageReader &operator=(const ImageReader &);
};

#endif // IMAGEREADER_H
