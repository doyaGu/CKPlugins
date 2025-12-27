#ifndef TGAREADER_H
#define TGAREADER_H

#include "ImageReader.h"

// TGA Reader GUID (from original binary)
#define TGAREADER_GUID CKGUID(0xBCA97223, 0x48578BCA)

/**
 * TgaReader - Truevision TGA format reader/writer
 *
 * Faithfully implements the original Virtools TgaReader including:
 *   - Reading: 8/15/16/24/32-bit TGA files
 *   - Image types 1, 2, 3 (uncompressed) and 9, 10, 11 (RLE compressed)
 *   - Color-mapped (paletted), grayscale, and true-color images
 *   - Writing: 24/32-bit TGA with optional RLE compression
 *
 * Original binary layout:
 *   - vtable: 4 bytes (offset 0)
 *   - m_Properties: 80 bytes (TgaBitmapProperties, offset 4)
 *   - Total: 84 bytes (0x54)
 *
 * Based on reverse-engineering of the original ImageReader.dll
 */
class TgaReader : public ImageReader
{
public:
    TgaReader();
    virtual ~TgaReader();

    virtual CKPluginInfo *GetReaderInfo();
    virtual int GetOptionsCount();
    virtual CKSTRING GetOptionDescription(int i);
    virtual CKBOOL IsAlphaSaved(CKBitmapProperties *bp);

    // Override to match original binary behavior (direct pass to TGA_Read/TGA_Save)
    virtual int ReadFile(CKSTRING filename, CKBitmapProperties **bp);
    virtual int ReadMemory(void *memory, int size, CKBitmapProperties **bp);
    virtual int SaveFile(CKSTRING filename, CKBitmapProperties *bp);
    virtual int SaveMemory(void **memory, CKBitmapProperties *bp);

private:
    // Extended properties (80 bytes) stored inline
    // m_Size = 80, m_BitDepth at offset 72, m_UseRLE at offset 76
    TgaBitmapProperties m_Properties;
};

//=============================================================================
// TGA file format structures
//=============================================================================
#pragma pack(push, 1)

struct TGAHEADER
{
    CKBYTE idLength;     // Length of image ID field
    CKBYTE colorMapType; // 0=no colormap, 1=has colormap
    CKBYTE imageType;    // Image type code
    // Color map specification
    CKWORD colorMapOrigin; // First color map entry index
    CKWORD colorMapLength; // Number of color map entries
    CKBYTE colorMapDepth;  // Bits per color map entry (15/16/24/32)
    // Image specification
    CKWORD xOrigin;         // X origin of image
    CKWORD yOrigin;         // Y origin of image
    CKWORD width;           // Image width
    CKWORD height;          // Image height
    CKBYTE pixelDepth;      // Bits per pixel (8/15/16/24/32)
    CKBYTE imageDescriptor; // Image descriptor byte
};

#pragma pack(pop)

// TGA image types
#define TGA_TYPE_NO_IMAGE 0
#define TGA_TYPE_COLORMAP 1       // Uncompressed, color-mapped
#define TGA_TYPE_TRUECOLOR 2      // Uncompressed, true-color
#define TGA_TYPE_GRAYSCALE 3      // Uncompressed, grayscale
#define TGA_TYPE_RLE_COLORMAP 9   // RLE, color-mapped
#define TGA_TYPE_RLE_TRUECOLOR 10 // RLE, true-color
#define TGA_TYPE_RLE_GRAYSCALE 11 // RLE, grayscale

//=============================================================================
// Internal helper functions
//=============================================================================

// Core TGA read function
int TGA_Read(void *data, int size, CKBitmapProperties *props);

// Core TGA save function
int TGA_Save(void **outBuffer, CKBitmapProperties *props, int bitDepth, int useRLE);

#endif // TGAREADER_H
