#ifndef PCXREADER_H
#define PCXREADER_H

#include "ImageReader.h"

// PCX Reader GUID (from original binary)
#define PCXREADER_GUID CKGUID(0x585C7216, 0x33302657)

/**
 * PcxReader - ZSoft PCX format reader (read-only)
 *
 * Faithfully implements the original Virtools PcxReader including:
 *   - Reading: 1/4/8/24-bit PCX files
 *   - RLE decompression
 *   - Color plane handling
 *
 * NOTE: SaveFile and SaveMemory return 0 (not implemented in original)
 *
 * Original binary layout:
 *   - vtable: 4 bytes (offset 0)
 *   - m_Properties: 80 bytes (PcxBitmapProperties, offset 4)
 *   - Total: 84 bytes (0x54)
 *
 * Based on reverse-engineering of the original ImageReader.dll
 */
class PcxReader : public ImageReader
{
public:
    PcxReader();
    virtual ~PcxReader();

    virtual CKPluginInfo *GetReaderInfo();
    virtual int GetOptionsCount();
    virtual CKSTRING GetOptionDescription(int i);
    virtual CKBOOL IsAlphaSaved(CKBitmapProperties *bp);

    // Override to match original binary behavior
    virtual int ReadFile(CKSTRING filename, CKBitmapProperties **bp);
    virtual int ReadMemory(void *memory, int size, CKBitmapProperties **bp);
    virtual int SaveFile(CKSTRING filename, CKBitmapProperties *bp);
    virtual int SaveMemory(void **memory, CKBitmapProperties *bp);

private:
    // Extended properties (80 bytes) stored inline
    // m_Size = 80, m_BitDepth at offset 72, m_UseRLE at offset 76
    PcxBitmapProperties m_Properties;
};

//=============================================================================
// PCX file format structures
//=============================================================================
#pragma pack(push, 1)

struct PCXHEADER
{
    CKBYTE manufacturer; // 0x0A = ZSoft PCX
    CKBYTE version;      // PCX version
    CKBYTE encoding;     // 1 = RLE encoding
    CKBYTE bitsPerPixel; // Bits per pixel per plane
    CKWORD xMin;         // Image bounds
    CKWORD yMin;
    CKWORD xMax;
    CKWORD yMax;
    CKWORD hDPI;         // Horizontal DPI
    CKWORD vDPI;         // Vertical DPI
    CKBYTE colorMap[48]; // 16-color palette (RGB triplets)
    CKBYTE reserved;
    CKBYTE nPlanes;      // Number of color planes
    CKWORD bytesPerLine; // Bytes per scan line per plane
    CKWORD paletteInfo;  // 1 = color, 2 = grayscale
    CKWORD hScreenSize;  // (ignored)
    CKWORD vScreenSize;  // (ignored)
    CKBYTE padding[54];  // Pad to 128 bytes
};

#pragma pack(pop)

//=============================================================================
// Internal helper functions
//=============================================================================

// Core PCX read function
int PCX_Read(void *data, int size, CKBitmapProperties *props);

#endif // PCXREADER_H
