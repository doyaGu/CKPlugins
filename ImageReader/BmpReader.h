#ifndef BMPREADER_H
#define BMPREADER_H

#include "ImageReader.h"

// BMP Reader GUID (from original binary)
#define BMPREADER_GUID CKGUID(0x614A243F, 0x437B3343)

/**
 * BmpReader - Windows Bitmap format reader/writer
 *
 * Faithfully implements the original Virtools BmpReader including:
 *   - Reading: 1/4/8/16/24/32-bit BMP files
 *   - RLE8 and RLE4 decompression
 *   - Writing: 8/24/32-bit BMP files with optional RLE8 compression
 *   - Proper color table handling for indexed formats
 *
 * Original binary layout:
 *   - vtable: 4 bytes (offset 0)
 *   - m_Properties: 76 bytes (BmpBitmapProperties, offset 4)
 *   - Total: 80 bytes (0x50)
 *
 * Based on reverse-engineering of the original ImageReader.dll
 */
class BmpReader : public ImageReader
{
public:
    BmpReader();
    virtual ~BmpReader();

    virtual CKPluginInfo *GetReaderInfo();
    virtual int GetOptionsCount();
    virtual CKSTRING GetOptionDescription(int i);
    virtual CKBOOL IsAlphaSaved(CKBitmapProperties *bp);

    // Override to match original binary behavior (direct pass to BMP_Read/BMP_Save)
    virtual int ReadFile(CKSTRING filename, CKBitmapProperties **bp);
    virtual int ReadMemory(void *memory, int size, CKBitmapProperties **bp);
    virtual int SaveFile(CKSTRING filename, CKBitmapProperties *bp);
    virtual int SaveMemory(void **memory, CKBitmapProperties *bp);

private:
    // Extended properties (76 bytes) stored inline
    // m_Size = 76, m_BitDepth at offset 72
    BmpBitmapProperties m_Properties;
};

//=============================================================================
// Internal BMP file format structures
//=============================================================================
#pragma pack(push, 1)

struct BITMAPFILEHEADER
{
    CKWORD bfType;  // "BM" signature (0x4D42)
    CKDWORD bfSize; // File size
    CKWORD bfReserved1;
    CKWORD bfReserved2;
    CKDWORD bfOffBits; // Offset to pixel data
};

struct BITMAPINFOHEADER
{
    CKDWORD biSize;        // Header size (40 for BITMAPINFOHEADER)
    CKDWORD biWidth;       // Image width (signed in original, but stored as DWORD)
    CKDWORD biHeight;      // Image height (can be negative for top-down)
    CKWORD biPlanes;       // Number of planes (always 1)
    CKWORD biBitCount;     // Bits per pixel
    CKDWORD biCompression; // Compression type
    CKDWORD biSizeImage;   // Image size (can be 0 for uncompressed)
    CKDWORD biXPelsPerMeter;
    CKDWORD biYPelsPerMeter;
    CKDWORD biClrUsed;      // Colors used
    CKDWORD biClrImportant; // Important colors
};

// OS/2 BITMAPCOREHEADER (12 bytes)
struct BITMAPCOREHEADER
{
    CKDWORD bcSize;
    CKWORD bcWidth;
    CKWORD bcHeight;
    CKWORD bcPlanes;
    CKWORD bcBitCount;
};

#pragma pack(pop)

// BMP compression types
#define BI_RGB 0       // Uncompressed
#define BI_RLE8 1      // RLE 8-bit
#define BI_RLE4 2      // RLE 4-bit
#define BI_BITFIELDS 3 // Bitfields (16/32-bit)
// Additional Windows BMP compression types
// (Used by BITMAPV4HEADER/BITMAPV5HEADER and some BI_BITFIELDS files)
#define BI_JPEG 4           // JPEG image for printing (not a DIB pixel array)
#define BI_PNG 5            // PNG image for printing (not a DIB pixel array)
#define BI_ALPHABITFIELDS 6 // Bitfields with alpha channel mask

//=============================================================================
// Internal helper functions (implemented in BmpReader.cpp)
//=============================================================================

// Core BMP read function - reads from file path or memory buffer
// If size == 0, treats data as filename; otherwise treats as memory buffer
int BMP_Read(void *data, int size, CKBitmapProperties *props);

// Core BMP save function - saves to file or returns memory buffer
// If *outBuffer is non-NULL, treats as filename to save; otherwise allocates and returns buffer
int BMP_Save(void **outBuffer, CKBitmapProperties *props, int bitDepth);

#endif // BMPREADER_H
