#include "PcxReader.h"

#include "XArray.h"

//=============================================================================
// Data Source Abstraction
//=============================================================================
class PcxDataSource
{
public:
    virtual ~PcxDataSource() {}
    virtual CKBOOL ReadHeader(PCXHEADER &hdr) = 0;
    virtual CKBOOL ReadRemaining(XArray<CKBYTE> &out) = 0;
};

class PcxFileSource : public PcxDataSource
{
    FILE *m_fp;

public:
    PcxFileSource(const char *filename) : m_fp(fopen(filename, "rb")) {}
    ~PcxFileSource()
    {
        if (m_fp)
            fclose(m_fp);
    }
    CKBOOL IsValid() const { return m_fp != NULL; }

    CKBOOL ReadHeader(PCXHEADER &hdr) override
    {
        return m_fp && fread(&hdr, 1, sizeof(hdr), m_fp) == sizeof(hdr);
    }

    CKBOOL ReadRemaining(XArray<CKBYTE> &out) override
    {
        if (!m_fp)
            return FALSE;
        long cur = ftell(m_fp);
        if (cur < 0)
            return FALSE;
        fseek(m_fp, 0, SEEK_END);
        long end = ftell(m_fp);
        if (end < 0 || end < cur)
            return FALSE;
        fseek(m_fp, cur, SEEK_SET);
        CKDWORD remaining = (CKDWORD)(end - cur);
        if (remaining == 0)
            return TRUE;
        out.Resize((int)remaining);
        return fread(out.Begin(), 1, remaining, m_fp) == remaining;
    }
};

class PcxMemorySource : public PcxDataSource
{
    const CKBYTE *m_data;
    int m_size;
    CKDWORD m_offset;

public:
    PcxMemorySource(const void *data, int size)
        : m_data((const CKBYTE *)data), m_size(size), m_offset(0) {}

    CKBOOL ReadHeader(PCXHEADER &hdr) override
    {
        if (m_size < (int)sizeof(hdr))
            return FALSE;
        memcpy(&hdr, m_data, sizeof(hdr));
        m_offset = sizeof(hdr);
        return TRUE;
    }

    CKBOOL ReadRemaining(XArray<CKBYTE> &out) override
    {
        if (m_offset >= (CKDWORD)m_size)
            return TRUE;
        CKDWORD remaining = (CKDWORD)m_size - m_offset;
        out.Resize((int)remaining);
        memcpy(out.Begin(), m_data + m_offset, remaining);
        return TRUE;
    }
};

//=============================================================================
// Utility Functions
//=============================================================================
static CKDWORD MinDword(CKDWORD a, CKDWORD b) { return (a < b) ? a : b; }

static CKBOOL IsAllZero(const CKBYTE *bytes, int count)
{
    for (int i = 0; i < count; i++)
        if (bytes[i] != 0)
            return FALSE;
    return TRUE;
}

//=============================================================================
// Default EGA Palette
//=============================================================================
static const CKBYTE EgaPalette[16][3] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
    {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
    {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF}
};

static void GetPal16(const PCXHEADER &hdr, CKBOOL useDefault, CKDWORD idx, CKBYTE &r, CKBYTE &g, CKBYTE &b)
{
    idx &= 15;
    if (useDefault)
    {
        r = EgaPalette[idx][0];
        g = EgaPalette[idx][1];
        b = EgaPalette[idx][2];
    }
    else
    {
        r = hdr.colorMap[idx * 3 + 0];
        g = hdr.colorMap[idx * 3 + 1];
        b = hdr.colorMap[idx * 3 + 2];
    }
}

//=============================================================================
// PCX Context & Scanline Decoding
//=============================================================================
struct PcxContext
{
    PCXHEADER header;
    CKDWORD width, height, bytesPerScanLine;
    CKBOOL isPlanar1bpp, isPacked2bpp, isPacked4bpp;
    CKBOOL isIndexed8bpp, isTrueColor24, isTrueColor32;
    CKBOOL forceDefaultEga;
    XArray<CKBYTE> fileData;
    CKDWORD srcPos;

    PcxContext() : width(0), height(0), bytesPerScanLine(0),
                   isPlanar1bpp(FALSE), isPacked2bpp(FALSE), isPacked4bpp(FALSE),
                   isIndexed8bpp(FALSE), isTrueColor24(FALSE), isTrueColor32(FALSE),
                   forceDefaultEga(FALSE), srcPos(0)
    {
        memset(&header, 0, sizeof(header));
    }

    void DecodeScanLine(CKBYTE *out)
    {
        memset(out, 0, bytesPerScanLine);
        if (!fileData.Begin())
            return;
        CKDWORD dataSize = (CKDWORD)fileData.Size();

        if (header.encoding == 0)
        {
            CKDWORD remaining = (srcPos < dataSize) ? (dataSize - srcPos) : 0;
            CKDWORD toCopy = MinDword(remaining, bytesPerScanLine);
            if (toCopy)
                memcpy(out, fileData.Begin() + srcPos, toCopy);
            srcPos += toCopy;
            return;
        }

        // RLE decoding
        CKDWORD linePos = 0;
        while (linePos < bytesPerScanLine && srcPos < dataSize)
        {
            CKBYTE byte = fileData.Begin()[srcPos++];
            if ((byte & 0xC0) == 0xC0)
            {
                CKDWORD count = byte & 0x3F;
                if (count == 0)
                    count = 1;
                if (srcPos >= dataSize)
                    break;
                CKBYTE value = fileData.Begin()[srcPos++];
                CKDWORD toWrite = MinDword(count, bytesPerScanLine - linePos);
                memset(out + linePos, value, toWrite);
                linePos += toWrite;
            }
            else
            {
                out[linePos++] = byte;
            }
        }
    }
};

static int ParsePcxHeader(PcxDataSource &src, PcxContext &ctx)
{
    if (!src.ReadHeader(ctx.header))
        return CKBITMAPERROR_READERROR;

    if (ctx.header.manufacturer != 0x0A)
        return CKBITMAPERROR_UNSUPPORTEDFILE;
    if (ctx.header.encoding != 0 && ctx.header.encoding != 1)
        return CKBITMAPERROR_UNSUPPORTEDFILE;

    ctx.width = ctx.header.xMax - ctx.header.xMin + 1;
    ctx.height = ctx.header.yMax - ctx.header.yMin + 1;

    if (ctx.width == 0 || ctx.height == 0)
        return CKBITMAPERROR_FILECORRUPTED;
    if (ctx.width > 32768 || ctx.height > 32768)
        return CKBITMAPERROR_FILECORRUPTED;
    if (ctx.header.bitsPerPixel == 0 || ctx.header.nPlanes == 0 || ctx.header.bytesPerLine == 0)
        return CKBITMAPERROR_FILECORRUPTED;

    CKDWORD bpp = ctx.header.bitsPerPixel;
    CKDWORD planes = ctx.header.nPlanes;

    uint64_t bps64 = (uint64_t)ctx.header.bytesPerLine * planes;
    if (bps64 > 0x7FFFFFFFULL)
        return CKBITMAPERROR_FILECORRUPTED;
    ctx.bytesPerScanLine = (CKDWORD)bps64;

    ctx.isPlanar1bpp = (bpp == 1 && planes >= 1 && planes <= 4);
    ctx.isPacked2bpp = (bpp == 2 && planes == 1);
    ctx.isPacked4bpp = (bpp == 4 && planes == 1);
    ctx.isIndexed8bpp = (bpp == 8 && planes == 1);
    ctx.isTrueColor24 = (bpp == 8 && planes == 3);
    ctx.isTrueColor32 = (bpp == 8 && planes == 4);

    if (!(ctx.isPlanar1bpp || ctx.isPacked2bpp || ctx.isPacked4bpp ||
          ctx.isIndexed8bpp || ctx.isTrueColor24 || ctx.isTrueColor32))
        return CKBITMAPERROR_UNSUPPORTEDFILE;

    ctx.forceDefaultEga = (ctx.header.version == 0 || ctx.header.version == 3 ||
                           IsAllZero(ctx.header.colorMap, 48));

    return 0;
}

//=============================================================================
// Row Decoders
//=============================================================================
static void DecodeRowPlanar1bpp(const PcxContext &ctx, CKDWORD width, const CKBYTE *scanLine, CKBYTE *dstRow)
{
    CKDWORD nPlanes = ctx.header.nPlanes;
    CKDWORD maxX = MinDword(width, ctx.header.bytesPerLine * 8);

    for (CKDWORD x = 0; x < maxX; x++)
    {
        CKDWORD byteOff = x / 8;
        CKDWORD bitOff = 7 - (x & 7);
        CKDWORD idx = 0;
        for (CKDWORD p = 0; p < nPlanes; p++)
            idx |= ((scanLine[p * ctx.header.bytesPerLine + byteOff] >> bitOff) & 1) << p;

        CKBYTE r, g, b;
        if (ctx.header.paletteInfo == 2 && nPlanes == 1)
        {
            r = g = b = (idx & 1) ? 255 : 0;
        }
        else
        {
            GetPal16(ctx.header, ctx.forceDefaultEga, idx, r, g, b);
        }
        dstRow[x * 4 + 0] = b;
        dstRow[x * 4 + 1] = g;
        dstRow[x * 4 + 2] = r;
        dstRow[x * 4 + 3] = 255;
    }
}

static void DecodeRowPacked2bpp(const PcxContext &ctx, CKDWORD width, const CKBYTE *scanLine, CKBYTE *dstRow)
{
    CKDWORD maxX = MinDword(width, ctx.header.bytesPerLine * 4);
    for (CKDWORD x = 0; x < maxX; x++)
    {
        CKBYTE byteVal = scanLine[x / 4];
        CKDWORD shift = 6 - 2 * (x & 3);
        CKDWORD idx = (byteVal >> shift) & 3;
        CKBYTE r, g, b;
        GetPal16(ctx.header, ctx.forceDefaultEga, idx, r, g, b);
        dstRow[x * 4 + 0] = b;
        dstRow[x * 4 + 1] = g;
        dstRow[x * 4 + 2] = r;
        dstRow[x * 4 + 3] = 255;
    }
}

static void DecodeRowPacked4bpp(const PcxContext &ctx, CKDWORD width, const CKBYTE *scanLine, CKBYTE *dstRow)
{
    CKDWORD maxX = MinDword(width, ctx.header.bytesPerLine * 2);
    for (CKDWORD x = 0; x < maxX; x++)
    {
        CKBYTE byteVal = scanLine[x / 2];
        CKDWORD idx = (x & 1) ? (byteVal & 0x0F) : (byteVal >> 4);
        CKBYTE r, g, b;
        GetPal16(ctx.header, ctx.forceDefaultEga, idx, r, g, b);
        dstRow[x * 4 + 0] = b;
        dstRow[x * 4 + 1] = g;
        dstRow[x * 4 + 2] = r;
        dstRow[x * 4 + 3] = 255;
    }
}

static void DecodeRowTrueColor24(const PcxContext &ctx, CKDWORD width, const CKBYTE *scanLine, CKBYTE *dstRow)
{
    CKDWORD maxX = MinDword(width, ctx.header.bytesPerLine);
    for (CKDWORD x = 0; x < maxX; x++)
    {
        dstRow[x * 4 + 2] = scanLine[x];                               // R
        dstRow[x * 4 + 1] = scanLine[ctx.header.bytesPerLine + x];     // G
        dstRow[x * 4 + 0] = scanLine[ctx.header.bytesPerLine * 2 + x]; // B
        dstRow[x * 4 + 3] = 255;
    }
}

static void DecodeRowTrueColor32(const PcxContext &ctx, CKDWORD width, const CKBYTE *scanLine, CKBYTE *dstRow)
{
    CKDWORD maxX = MinDword(width, ctx.header.bytesPerLine);
    for (CKDWORD x = 0; x < maxX; x++)
    {
        dstRow[x * 4 + 2] = scanLine[x];                               // R
        dstRow[x * 4 + 1] = scanLine[ctx.header.bytesPerLine + x];     // G
        dstRow[x * 4 + 0] = scanLine[ctx.header.bytesPerLine * 2 + x]; // B
        dstRow[x * 4 + 3] = scanLine[ctx.header.bytesPerLine * 3 + x]; // A
    }
}

//=============================================================================
// VGA Palette Location
//=============================================================================
static const CKBYTE *FindVgaPalette(const CKBYTE *data, CKDWORD dataSize, CKDWORD imageEndPos)
{
    if (!data || dataSize < 769)
        return NULL;
    // Preferred: marker immediately after image data
    if (imageEndPos + 769 <= dataSize && data[imageEndPos] == 0x0C)
        return data + imageEndPos + 1;
    // Fallback: marker at end of file
    if (data[dataSize - 769] == 0x0C)
        return data + dataSize - 768;
    return NULL;
}

static void ApplyIndexedPalette(CKDWORD width, CKDWORD height, CKDWORD stride,
                                const CKBYTE *indexPixels, CKBYTE *dstPixels,
                                const CKBYTE *vgaPal, CKBOOL grayscale)
{
    for (CKDWORD y = 0; y < height; y++)
    {
        CKBYTE *dstRow = dstPixels + y * stride;
        const CKBYTE *idxRow = indexPixels + y * width;
        for (CKDWORD x = 0; x < width; x++)
        {
            CKBYTE idx = idxRow[x];
            if (!grayscale && vgaPal)
            {
                dstRow[x * 4 + 0] = vgaPal[idx * 3 + 2]; // B
                dstRow[x * 4 + 1] = vgaPal[idx * 3 + 1]; // G
                dstRow[x * 4 + 2] = vgaPal[idx * 3 + 0]; // R
            }
            else
            {
                dstRow[x * 4 + 0] = dstRow[x * 4 + 1] = dstRow[x * 4 + 2] = idx;
            }
            dstRow[x * 4 + 3] = 255;
        }
    }
}

//=============================================================================
// PcxReader Class Implementation
//=============================================================================
PcxReader::PcxReader() : ImageReader()
{
    m_Properties.Init(PCXREADER_GUID, "pcx");
}

PcxReader::~PcxReader()
{
    FreeBitmapData(&m_Properties);
}

CKPluginInfo *PcxReader::GetReaderInfo()
{
    return &g_PluginInfo[READER_INDEX_PCX];
}

int PcxReader::GetOptionsCount() { return 0; }

CKSTRING PcxReader::GetOptionDescription(int i)
{
    if (i == 0)
        return "Enum:Bit Depth:1 bit=1,4 bit=4,8 bit=8,24 bit=24";
    if (i == 1)
        return "Boolean:Run Length Encoding";
    return "";
}

CKBOOL PcxReader::IsAlphaSaved(CKBitmapProperties *bp) { return FALSE; }

int PcxReader::ReadFile(CKSTRING filename, CKBitmapProperties **bp)
{
    if (!filename || !bp)
        return 1;
    int result = PCX_Read((void *)filename, 0, (CKBitmapProperties *)&m_Properties);
    *bp = (CKBitmapProperties *)&m_Properties;
    return result;
}

int PcxReader::ReadMemory(void *memory, int size, CKBitmapProperties **bp)
{
    if (!bp)
        return 1;
    int result = PCX_Read(memory, size, (CKBitmapProperties *)&m_Properties);
    *bp = (CKBitmapProperties *)&m_Properties;
    return result;
}

int PcxReader::SaveFile(CKSTRING filename, CKBitmapProperties *bp) { return 0; }

int PcxReader::SaveMemory(void **memory, CKBitmapProperties *bp)
{
    if (memory && bp)
        *memory = NULL;
    return 0;
}

//=============================================================================
// PCX_Read - Core Reading Function
//=============================================================================
int PCX_Read(void *data, int size, CKBitmapProperties *props)
{
    if (!data || !props)
        return CKBITMAPERROR_GENERIC;
    if (size < 0)
        return CKBITMAPERROR_FILECORRUPTED;

    // Create data source
    PcxDataSource *src = (size == 0)
                             ? (PcxDataSource *)new PcxFileSource((const char *)data)
                             : (PcxDataSource *)new PcxMemorySource(data, size);

    if (size == 0 && !((PcxFileSource *)src)->IsValid())
    {
        delete src;
        return CKBITMAPERROR_READERROR;
    }

    PcxContext ctx;
    int result = ParsePcxHeader(*src, ctx);
    if (result != 0)
    {
        delete src;
        return result;
    }

    // Read remaining file data
    if (!src->ReadRemaining(ctx.fileData))
    {
        delete src;
        return CKBITMAPERROR_READERROR;
    }
    delete src;
    src = NULL;

    // Allocate destination
    CKDWORD dstStride = ctx.width * 4;
    uint64_t dstSize64 = (uint64_t)dstStride * ctx.height;
    if (dstSize64 > 0x7FFFFFFFULL)
        return CKBITMAPERROR_FILECORRUPTED;

    CKBYTE *dstPixels = new CKBYTE[(CKDWORD)dstSize64];
    memset(dstPixels, 0, (CKDWORD)dstSize64);
    for (CKDWORD i = 0; i < (CKDWORD)dstSize64; i += 4)
        dstPixels[i + 3] = 255;

    // Allocate scanline buffer
    XArray<CKBYTE> scanLine((int)ctx.bytesPerScanLine);
    scanLine.Resize((int)ctx.bytesPerScanLine);

    // Allocate index buffer for 8bpp
    CKBYTE *indexPixels = NULL;
    if (ctx.isIndexed8bpp)
    {
        uint64_t idxSize = (uint64_t)ctx.width * ctx.height;
        if (idxSize > 0x7FFFFFFFULL)
        {
            delete[] dstPixels;
            return CKBITMAPERROR_FILECORRUPTED;
        }
        indexPixels = new CKBYTE[(CKDWORD)idxSize];
        memset(indexPixels, 0, (CKDWORD)idxSize);
    }

    // Decode scanlines
    ctx.srcPos = 0;
    for (CKDWORD y = 0; y < ctx.height; y++)
    {
        ctx.DecodeScanLine(scanLine.Begin());
        CKBYTE *dstRow = dstPixels + y * dstStride;

        if (ctx.isIndexed8bpp)
        {
            CKDWORD maxX = MinDword(ctx.width, ctx.header.bytesPerLine);
            CKBYTE *idxRow = indexPixels + y * ctx.width;
            for (CKDWORD x = 0; x < maxX; x++)
                idxRow[x] = scanLine.Begin()[x];
        }
        else if (ctx.isTrueColor24)
        {
            DecodeRowTrueColor24(ctx, ctx.width, scanLine.Begin(), dstRow);
        }
        else if (ctx.isTrueColor32)
        {
            DecodeRowTrueColor32(ctx, ctx.width, scanLine.Begin(), dstRow);
        }
        else if (ctx.isPlanar1bpp)
        {
            DecodeRowPlanar1bpp(ctx, ctx.width, scanLine.Begin(), dstRow);
        }
        else if (ctx.isPacked2bpp)
        {
            DecodeRowPacked2bpp(ctx, ctx.width, scanLine.Begin(), dstRow);
        }
        else if (ctx.isPacked4bpp)
        {
            DecodeRowPacked4bpp(ctx, ctx.width, scanLine.Begin(), dstRow);
        }
    }

    // Apply VGA palette for 8bpp
    if (ctx.isIndexed8bpp)
    {
        const CKBYTE *vgaPal = FindVgaPalette(ctx.fileData.Begin(), (CKDWORD)ctx.fileData.Size(), ctx.srcPos);
        CKBOOL grayscale = (ctx.header.paletteInfo == 2);
        ApplyIndexedPalette(ctx.width, ctx.height, dstStride, indexPixels, dstPixels, vgaPal, grayscale);
        delete[] indexPixels;
    }

    // Fill properties
    ImageReader::FillFormatBGRA32(props->m_Format, (int)ctx.width, (int)ctx.height, (int)dstStride, dstPixels);
    props->m_Data = dstPixels;
    return 0;
}
