#include "BmpReader.h"

#include "XArray.h"

//=============================================================================
// Data Source Abstraction - Unified file/memory reading
//=============================================================================
class BmpDataSource
{
public:
    virtual ~BmpDataSource() {}
    virtual CKBOOL Read(void *buffer, CKDWORD size) = 0;
    virtual CKBOOL Seek(CKDWORD offset) = 0;
    virtual CKBOOL SeekRelative(int offset) = 0;
    virtual CKDWORD Tell() const = 0;
    virtual CKDWORD Size() const = 0;
    virtual CKBOOL ReadAt(CKDWORD offset, void *buffer, CKDWORD size) = 0;
};

class BmpFileSource : public BmpDataSource
{
    FILE *m_fp;
    CKDWORD m_size;

public:
    BmpFileSource(const char *filename) : m_fp(NULL), m_size(0)
    {
        m_fp = fopen(filename, "rb");
        if (m_fp)
        {
            fseek(m_fp, 0, SEEK_END);
            long sz = ftell(m_fp);
            m_size = (sz > 0) ? (CKDWORD)sz : 0;
            fseek(m_fp, 0, SEEK_SET);
        }
    }
    ~BmpFileSource()
    {
        if (m_fp)
            fclose(m_fp);
    }
    CKBOOL IsValid() const { return m_fp != NULL; }

    CKBOOL Read(void *buffer, CKDWORD size) override
    {
        return m_fp && fread(buffer, 1, size, m_fp) == size;
    }
    CKBOOL Seek(CKDWORD offset) override
    {
        return m_fp && fseek(m_fp, (long)offset, SEEK_SET) == 0;
    }
    CKBOOL SeekRelative(int offset) override
    {
        return m_fp && fseek(m_fp, offset, SEEK_CUR) == 0;
    }
    CKDWORD Tell() const override
    {
        if (!m_fp)
            return 0;
        long pos = ftell(m_fp);
        return (pos >= 0) ? (CKDWORD)pos : 0;
    }
    CKDWORD Size() const override { return m_size; }
    CKBOOL ReadAt(CKDWORD offset, void *buffer, CKDWORD size) override
    {
        if (!m_fp)
            return FALSE;
        long cur = ftell(m_fp);
        if (fseek(m_fp, (long)offset, SEEK_SET) != 0)
            return FALSE;
        CKBOOL ok = (fread(buffer, 1, size, m_fp) == size);
        fseek(m_fp, cur, SEEK_SET);
        return ok;
    }
};

class BmpMemorySource : public BmpDataSource
{
    const CKBYTE *m_data;
    CKDWORD m_size;
    CKDWORD m_offset;

public:
    BmpMemorySource(const void *data, int size)
        : m_data((const CKBYTE *)data), m_size(size > 0 ? (CKDWORD)size : 0), m_offset(0) {}

    CKBOOL Read(void *buffer, CKDWORD size) override
    {
        if (m_offset + size > m_size)
            return FALSE;
        memcpy(buffer, m_data + m_offset, size);
        m_offset += size;
        return TRUE;
    }
    CKBOOL Seek(CKDWORD offset) override
    {
        if (offset > m_size)
            return FALSE;
        m_offset = offset;
        return TRUE;
    }
    CKBOOL SeekRelative(int offset) override
    {
        CKDWORD newOff = (offset < 0 && (CKDWORD)(-offset) > m_offset)
                             ? 0
                             : m_offset + offset;
        if (newOff > m_size)
            return FALSE;
        m_offset = newOff;
        return TRUE;
    }
    CKDWORD Tell() const override { return m_offset; }
    CKDWORD Size() const override { return m_size; }
    CKBOOL ReadAt(CKDWORD offset, void *buffer, CKDWORD size) override
    {
        if (offset + size > m_size)
            return FALSE;
        memcpy(buffer, m_data + offset, size);
        return TRUE;
    }
};

//=============================================================================
// Templated Bitfield Extraction
//=============================================================================
template <typename T>
static CKBYTE ExtractMaskedComponent(T value, CKDWORD mask)
{
    if (!mask)
        return 0;
    int shift = 0;
    CKDWORD m = mask;
    while ((m & 1) == 0)
    {
        m >>= 1;
        ++shift;
    }
    CKDWORD shifted = ((CKDWORD)value >> shift) & m;
    return (CKBYTE)((shifted * 255) / m);
}

//=============================================================================
// Unified Palette Lookup
//=============================================================================
static void SetBGRAFromPalette(XBYTE *row, CKDWORD x, CKBYTE idx,
                               const XBYTE *palette, CKBOOL is3Byte, CKDWORD entries)
{
    if (idx >= entries)
        idx = 0;
    CKDWORD stride = is3Byte ? 3 : 4;
    row[x * 4 + 0] = palette[idx * stride + 0];
    row[x * 4 + 1] = palette[idx * stride + 1];
    row[x * 4 + 2] = palette[idx * stride + 2];
    row[x * 4 + 3] = 255;
}

//=============================================================================
// RLE Decoding Context
//=============================================================================
struct RLEContext
{
    const XBYTE *src;
    CKDWORD srcSize;
    CKDWORD srcPos;
    XBYTE *dst;
    CKDWORD dstStride;
    CKDWORD width;
    CKDWORD height;
    CKBOOL topDown;
    const XBYTE *palette;
    CKBOOL is3BytePalette;
    CKDWORD paletteEntries;
    CKDWORD x;
    CKDWORD y;

    RLEContext(const XBYTE *s, CKDWORD ss, XBYTE *d, CKDWORD ds, CKDWORD w, CKDWORD h,
               CKBOOL td, const XBYTE *pal, CKBOOL is3, CKDWORD pe)
        : src(s), srcSize(ss), srcPos(0), dst(d), dstStride(ds),
          width(w), height(h), topDown(td), palette(pal),
          is3BytePalette(is3), paletteEntries(pe),
          x(0), y(td ? 0 : h - 1) {}

    XBYTE *Row() { return (y < height) ? (dst + y * dstStride) : NULL; }
    void NextLine()
    {
        x = 0;
        if (topDown)
            y++;
        else if (y > 0)
            y--;
        else
            y = height;
    }
    void Delta(CKBYTE dx, CKBYTE dy)
    {
        x += dx;
        if (topDown)
            y += dy;
        else
            y -= dy;
    }
    CKBOOL HasMore() const { return srcPos < srcSize && y < height; }
    CKBYTE ReadByte() { return (srcPos < srcSize) ? src[srcPos++] : 0; }
    void SetPixel(CKBYTE idx)
    {
        XBYTE *row = Row();
        if (row && x < width)
            SetBGRAFromPalette(row, x++, idx, palette, is3BytePalette, paletteEntries);
    }
};

static void DecodeRLE8(RLEContext &ctx)
{
    while (ctx.HasMore())
    {
        CKBYTE first = ctx.ReadByte();
        CKBYTE second = ctx.ReadByte();
        if (first == 0)
        {
            if (second == 0)
                ctx.NextLine();
            else if (second == 1)
                return;
            else if (second == 2)
                ctx.Delta(ctx.ReadByte(), ctx.ReadByte());
            else
            {
                for (CKDWORD i = 0; i < second; i++)
                    ctx.SetPixel(ctx.ReadByte());
                if (second & 1)
                    ctx.srcPos++;
            }
        }
        else
        {
            for (CKDWORD i = 0; i < first; i++)
                ctx.SetPixel(second);
        }
    }
}

static void DecodeRLE4(RLEContext &ctx)
{
    while (ctx.HasMore())
    {
        CKBYTE first = ctx.ReadByte();
        CKBYTE second = ctx.ReadByte();
        if (first == 0)
        {
            if (second == 0)
                ctx.NextLine();
            else if (second == 1)
                return;
            else if (second == 2)
                ctx.Delta(ctx.ReadByte(), ctx.ReadByte());
            else
            {
                for (CKDWORD i = 0; i < second; i++)
                {
                    CKBYTE idx = ((i & 1) == 0) ? (ctx.src[ctx.srcPos] >> 4)
                                                : (ctx.src[ctx.srcPos++] & 0x0F);
                    ctx.SetPixel(idx);
                }
                if (second & 1)
                    ctx.srcPos++;
                if (((second + 1) / 2) & 1)
                    ctx.srcPos++;
            }
        }
        else
        {
            CKBYTE idx1 = second >> 4, idx2 = second & 0x0F;
            for (CKDWORD i = 0; i < first; i++)
                ctx.SetPixel((i & 1) ? idx2 : idx1);
        }
    }
}

//=============================================================================
// Row Decoders
//=============================================================================
static void DecodeRow1bpp(const XBYTE *src, XBYTE *dst, CKDWORD width,
                          const XBYTE *pal, CKBOOL is3, CKDWORD entries)
{
    for (CKDWORD x = 0; x < width; x++)
        SetBGRAFromPalette(dst, x, (src[x / 8] >> (7 - (x & 7))) & 1, pal, is3, entries);
}

static void DecodeRow4bpp(const XBYTE *src, XBYTE *dst, CKDWORD width,
                          const XBYTE *pal, CKBOOL is3, CKDWORD entries)
{
    for (CKDWORD x = 0; x < width; x++)
        SetBGRAFromPalette(dst, x, (x & 1) ? (src[x / 2] & 0x0F) : (src[x / 2] >> 4), pal, is3, entries);
}

static void DecodeRow8bpp(const XBYTE *src, XBYTE *dst, CKDWORD width,
                          const XBYTE *pal, CKBOOL is3, CKDWORD entries)
{
    for (CKDWORD x = 0; x < width; x++)
        SetBGRAFromPalette(dst, x, src[x], pal, is3, entries);
}

static void DecodeRow16bpp(const XBYTE *src, XBYTE *dst, CKDWORD width,
                           CKDWORD rMask, CKDWORD gMask, CKDWORD bMask, CKDWORD aMask, CKBOOL useMasks)
{
    for (CKDWORD x = 0; x < width; x++)
    {
        CKWORD pixel = *(const CKWORD *)(src + x * 2);
        if (useMasks && rMask && gMask && bMask)
        {
            dst[x * 4 + 0] = ExtractMaskedComponent<CKWORD>(pixel, bMask);
            dst[x * 4 + 1] = ExtractMaskedComponent<CKWORD>(pixel, gMask);
            dst[x * 4 + 2] = ExtractMaskedComponent<CKWORD>(pixel, rMask);
            dst[x * 4 + 3] = aMask ? ExtractMaskedComponent<CKWORD>(pixel, aMask) : 255;
        }
        else
        {
            dst[x * 4 + 0] = (CKBYTE)(((pixel >> 0) & 0x1F) * 255 / 31);
            dst[x * 4 + 1] = (CKBYTE)(((pixel >> 5) & 0x1F) * 255 / 31);
            dst[x * 4 + 2] = (CKBYTE)(((pixel >> 10) & 0x1F) * 255 / 31);
            dst[x * 4 + 3] = 255;
        }
    }
}

static void DecodeRow24bpp(const XBYTE *src, XBYTE *dst, CKDWORD width)
{
    for (CKDWORD x = 0; x < width; x++)
    {
        dst[x * 4 + 0] = src[x * 3 + 0];
        dst[x * 4 + 1] = src[x * 3 + 1];
        dst[x * 4 + 2] = src[x * 3 + 2];
        dst[x * 4 + 3] = 255;
    }
}

static void DecodeRow32bpp(const XBYTE *src, XBYTE *dst, CKDWORD width,
                           CKDWORD rMask, CKDWORD gMask, CKDWORD bMask, CKDWORD aMask, CKBOOL useMasks)
{
    for (CKDWORD x = 0; x < width; x++)
    {
        CKDWORD pixel = *(const CKDWORD *)(src + x * 4);
        if (useMasks && rMask && gMask && bMask)
        {
            dst[x * 4 + 0] = ExtractMaskedComponent<CKDWORD>(pixel, bMask);
            dst[x * 4 + 1] = ExtractMaskedComponent<CKDWORD>(pixel, gMask);
            dst[x * 4 + 2] = ExtractMaskedComponent<CKDWORD>(pixel, rMask);
            dst[x * 4 + 3] = aMask ? ExtractMaskedComponent<CKDWORD>(pixel, aMask) : 255;
        }
        else
        {
            dst[x * 4 + 0] = src[x * 4 + 0];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 2];
            dst[x * 4 + 3] = 255;
        }
    }
}

//=============================================================================
// BMP Header Parsing
//=============================================================================
struct BmpHeader
{
    CKDWORD width, height;
    CKWORD bitCount, planes;
    CKDWORD compression, colorsUsed, headerSize;
    CKBOOL topDown;
    CKDWORD redMask, greenMask, blueMask, alphaMask;
    CKDWORD pixelDataOffset;

    BmpHeader() : width(0), height(0), bitCount(0), planes(0), compression(0),
                  colorsUsed(0), headerSize(0), topDown(FALSE),
                  redMask(0), greenMask(0), blueMask(0), alphaMask(0), pixelDataOffset(0) {}
};

static int ParseBmpHeader(BmpDataSource &src, BmpHeader &hdr)
{
    BITMAPFILEHEADER fileHdr;
    if (!src.Read(&fileHdr, sizeof(fileHdr)))
        return CKBITMAPERROR_READERROR;
    if (fileHdr.bfType != 0x4D42)
        return CKBITMAPERROR_UNSUPPORTEDFILE;
    hdr.pixelDataOffset = fileHdr.bfOffBits;

    if (!src.Read(&hdr.headerSize, 4))
        return CKBITMAPERROR_READERROR;

    if (hdr.headerSize == 12)
    {
        BITMAPCOREHEADER core;
        core.bcSize = hdr.headerSize;
        if (!src.Read(&core.bcWidth, 8))
            return CKBITMAPERROR_READERROR;
        hdr.width = core.bcWidth;
        hdr.height = core.bcHeight;
        hdr.planes = core.bcPlanes;
        hdr.bitCount = core.bcBitCount;
        hdr.compression = BI_RGB;
    }
    else if (hdr.headerSize >= 40)
    {
        BITMAPINFOHEADER info;
        info.biSize = hdr.headerSize;
        CKDWORD toRead = (hdr.headerSize > sizeof(info)) ? sizeof(info) - 4 : hdr.headerSize - 4;
        if (!src.Read(&info.biWidth, toRead))
            return CKBITMAPERROR_READERROR;
        if (hdr.headerSize > sizeof(info))
            src.SeekRelative((int)(hdr.headerSize - sizeof(info)));

        hdr.width = info.biWidth;
        if ((int)info.biHeight < 0)
        {
            hdr.height = -(int)info.biHeight;
            hdr.topDown = TRUE;
        }
        else
            hdr.height = info.biHeight;

        hdr.planes = info.biPlanes;
        hdr.bitCount = info.biBitCount;
        hdr.compression = info.biCompression;
        hdr.colorsUsed = info.biClrUsed;

        if (hdr.compression == BI_JPEG || hdr.compression == BI_PNG)
            return CKBITMAPERROR_UNSUPPORTEDFILE;
        if (hdr.compression != BI_RGB && hdr.compression != BI_RLE8 &&
            hdr.compression != BI_RLE4 && hdr.compression != BI_BITFIELDS &&
            hdr.compression != BI_ALPHABITFIELDS)
            return CKBITMAPERROR_UNSUPPORTEDFILE;

        // Read bitfield masks
        if ((hdr.compression == BI_BITFIELDS || hdr.compression == BI_ALPHABITFIELDS) && hdr.headerSize >= 52)
        {
            CKDWORD base = sizeof(BITMAPFILEHEADER) + 40;
            src.ReadAt(base, &hdr.redMask, 4);
            src.ReadAt(base + 4, &hdr.greenMask, 4);
            src.ReadAt(base + 8, &hdr.blueMask, 4);
            if (hdr.headerSize >= 56)
                src.ReadAt(base + 12, &hdr.alphaMask, 4);
        }
    }
    else
        return CKBITMAPERROR_UNSUPPORTEDFILE;

    // Validate
    if (hdr.width == 0 || hdr.height == 0)
        return CKBITMAPERROR_FILECORRUPTED;
    if (hdr.planes != 1)
        return CKBITMAPERROR_FILECORRUPTED;
    if (hdr.bitCount != 1 && hdr.bitCount != 4 && hdr.bitCount != 8 &&
        hdr.bitCount != 16 && hdr.bitCount != 24 && hdr.bitCount != 32)
        return CKBITMAPERROR_UNSUPPORTEDFILE;
    if (hdr.compression == BI_RLE8 && hdr.bitCount != 8)
        return CKBITMAPERROR_UNSUPPORTEDFILE;
    if (hdr.compression == BI_RLE4 && hdr.bitCount != 4)
        return CKBITMAPERROR_UNSUPPORTEDFILE;

    return 0;
}

//=============================================================================
// RLE8 Encoding (for save)
//=============================================================================
namespace RLE8
{
    static void EmitEncoded(XArray<CKBYTE> &out, CKBYTE count, CKBYTE value)
    {
        out.PushBack(count);
        out.PushBack(value);
    }

    static void EmitAbsolute(XArray<CKBYTE> &out, const CKBYTE *values, CKDWORD count)
    {
        out.PushBack(0);
        out.PushBack((CKBYTE)count);
        for (CKDWORD i = 0; i < count; i++)
            out.PushBack(values[i]);
        if (count & 1)
            out.PushBack(0);
    }

    static void EmitEOL(XArray<CKBYTE> &out)
    {
        out.PushBack(0);
        out.PushBack(0);
    }
    static void EmitEOB(XArray<CKBYTE> &out)
    {
        out.PushBack(0);
        out.PushBack(1);
    }

    static void EncodeRow(const CKBYTE *row, CKDWORD width, XArray<CKBYTE> &out)
    {
        CKDWORD x = 0;
        while (x < width)
        {
            CKBYTE v = row[x];
            CKDWORD runLen = 1;
            while (x + runLen < width && runLen < 255 && row[x + runLen] == v)
                runLen++;

            if (runLen >= 2)
            {
                EmitEncoded(out, (CKBYTE)runLen, v);
                x += runLen;
                continue;
            }

            CKDWORD litLen = 1;
            while (x + litLen < width && litLen < 255)
            {
                if (x + litLen + 1 < width && row[x + litLen] == row[x + litLen + 1])
                    break;
                litLen++;
            }

            if (litLen >= 3)
                EmitAbsolute(out, row + x, litLen);
            else
                for (CKDWORD i = 0; i < litLen; i++)
                    EmitEncoded(out, 1, row[x + i]);
            x += litLen;
        }
    }
}

static CKBYTE ToGray(CKBYTE b, CKBYTE g, CKBYTE r)
{
    return (CKBYTE)((r * 299 + g * 587 + b * 114) / 1000);
}

//=============================================================================
// BmpReader Class Implementation
//=============================================================================
BmpReader::BmpReader() : ImageReader()
{
    m_Properties.Init(BMPREADER_GUID, "bmp");
}

BmpReader::~BmpReader()
{
    FreeBitmapData(&m_Properties);
}

CKPluginInfo *BmpReader::GetReaderInfo()
{
    return &g_PluginInfo[READER_INDEX_BMP];
}

int BmpReader::GetOptionsCount() { return 1; }

CKSTRING BmpReader::GetOptionDescription(int i)
{
    return (i == 0) ? "Enum:Bit Depth:8 bit=8,8 bit RLE8 compression=9,16 bit=16,24 bit=24,32 bit=32" : "";
}

CKBOOL BmpReader::IsAlphaSaved(CKBitmapProperties *bp)
{
    if (!bp || bp->m_Size != 76)
        return FALSE;
    return ((CKDWORD *)bp)[18] == 32;
}

int BmpReader::ReadFile(CKSTRING filename, CKBitmapProperties **bp)
{
    if (!filename || !bp)
        return 1;
    int result = BMP_Read((void *)filename, 0, (CKBitmapProperties *)&m_Properties);
    *bp = (CKBitmapProperties *)&m_Properties;
    return result;
}

int BmpReader::ReadMemory(void *memory, int size, CKBitmapProperties **bp)
{
    if (!bp)
        return 1;
    int result = BMP_Read(memory, size, (CKBitmapProperties *)&m_Properties);
    *bp = (CKBitmapProperties *)&m_Properties;
    return result;
}

int BmpReader::SaveFile(CKSTRING filename, CKBitmapProperties *bp)
{
    if (!filename || !bp)
        return 0;
    BmpBitmapProperties local;
    memset(&local, 0, sizeof(local));
    memcpy(&local, bp, (bp->m_Size < sizeof(local)) ? bp->m_Size : sizeof(local));
    int depth = (bp->m_Size >= sizeof(BmpBitmapProperties)) ? (int)local.m_BitDepth : 24;
    void *ptr = (void *)filename;
    return BMP_Save(&ptr, (CKBitmapProperties *)&local, depth);
}

int BmpReader::SaveMemory(void **memory, CKBitmapProperties *bp)
{
    if (!memory || !bp)
        return 0;
    BmpBitmapProperties local;
    memset(&local, 0, sizeof(local));
    memcpy(&local, bp, (bp->m_Size < sizeof(local)) ? bp->m_Size : sizeof(local));
    int depth = (bp->m_Size >= sizeof(BmpBitmapProperties)) ? (int)local.m_BitDepth : 24;
    *memory = NULL;
    return BMP_Save(memory, (CKBitmapProperties *)&local, depth);
}

//=============================================================================
// BMP_Read - Core Reading Function
//=============================================================================
int BMP_Read(void *data, int size, CKBitmapProperties *props)
{
    if (!data || !props)
        return CKBITMAPERROR_GENERIC;
    if (size < 0)
        return CKBITMAPERROR_FILECORRUPTED;

    // Create data source
    BmpDataSource *src = (size == 0)
                             ? (BmpDataSource *)new BmpFileSource((const char *)data)
                             : (BmpDataSource *)new BmpMemorySource(data, size);

    if (size == 0 && !((BmpFileSource *)src)->IsValid())
    {
        delete src;
        return CKBITMAPERROR_READERROR;
    }

    // Parse header
    BmpHeader hdr;
    int result = ParseBmpHeader(*src, hdr);
    if (result != 0)
    {
        delete src;
        return result;
    }

    // Calculate palette
    CKDWORD paletteEntries = 0, paletteSize = 0;
    CKBOOL is3BytePalette = (hdr.headerSize == 12);

    if (hdr.bitCount <= 8)
    {
        CKDWORD maxColors = 1U << hdr.bitCount;
        paletteEntries = hdr.colorsUsed ? hdr.colorsUsed : maxColors;
        if (paletteEntries == 0 || paletteEntries > maxColors)
        {
            delete src;
            return CKBITMAPERROR_FILECORRUPTED;
        }
        paletteSize = paletteEntries * (is3BytePalette ? 3 : 4);
    }
    else if ((hdr.compression == BI_BITFIELDS || hdr.compression == BI_ALPHABITFIELDS) && hdr.headerSize < 52)
    {
        paletteSize = (hdr.compression == BI_ALPHABITFIELDS) ? 16 : 12;
    }

    // Read palette/masks
    XArray<XBYTE> palette;
    if (paletteSize > 0)
    {
        palette.Resize((int)paletteSize);
        if (!src->Read(palette.Begin(), paletteSize))
        {
            delete src;
            return CKBITMAPERROR_READERROR;
        }

        // Extract masks if stored after header
        if ((hdr.compression == BI_BITFIELDS || hdr.compression == BI_ALPHABITFIELDS) &&
            hdr.bitCount > 8 && hdr.headerSize < 52)
        {
            memcpy(&hdr.redMask, palette.Begin(), 4);
            memcpy(&hdr.greenMask, palette.Begin() + 4, 4);
            memcpy(&hdr.blueMask, palette.Begin() + 8, 4);
            if (paletteSize >= 16)
                memcpy(&hdr.alphaMask, palette.Begin() + 12, 4);
        }
    }

    // Validate and seek to pixel data
    if (hdr.pixelDataOffset < src->Tell())
    {
        delete src;
        return CKBITMAPERROR_FILECORRUPTED;
    }
    src->Seek(hdr.pixelDataOffset);

    // Calculate strides and sizes
    unsigned long long bitsPerRow = (unsigned long long)hdr.width * hdr.bitCount;
    unsigned long long srcStride64 = ((bitsPerRow + 31ULL) / 32ULL) * 4ULL;
    if (srcStride64 > 0xFFFFFFFFULL)
    {
        delete src;
        return CKBITMAPERROR_FILECORRUPTED;
    }
    CKDWORD srcStride = (CKDWORD)srcStride64;

    CKDWORD pixelDataSize = 0;
    if (hdr.compression == BI_RGB || hdr.compression == BI_BITFIELDS || hdr.compression == BI_ALPHABITFIELDS)
    {
        unsigned long long total = (unsigned long long)srcStride * hdr.height;
        if (total > 0xFFFFFFFFULL)
        {
            delete src;
            return CKBITMAPERROR_FILECORRUPTED;
        }
        pixelDataSize = (CKDWORD)total;
    }
    else
    {
        pixelDataSize = src->Size() - src->Tell();
    }

    // Read pixel data
    XArray<XBYTE> srcPixels;
    srcPixels.Resize((int)pixelDataSize);
    src->Read(srcPixels.Begin(), pixelDataSize);
    delete src;
    src = NULL;

    // Allocate destination
    CKDWORD dstStride = hdr.width * 4;
    unsigned long long dstTotal = (unsigned long long)dstStride * hdr.height;
    if (dstTotal > 0xFFFFFFFFULL)
        return CKBITMAPERROR_FILECORRUPTED;

    XBYTE *dstPixels = new XBYTE[(CKDWORD)dstTotal];
    memset(dstPixels, 0xFF, (CKDWORD)dstTotal);

    // Decode
    CKBOOL useMasks = (hdr.compression == BI_BITFIELDS || hdr.compression == BI_ALPHABITFIELDS);

    if (hdr.compression == BI_RLE8)
    {
        RLEContext ctx(srcPixels.Begin(), pixelDataSize, dstPixels, dstStride,
                       hdr.width, hdr.height, hdr.topDown, palette.Begin(),
                       is3BytePalette, paletteEntries);
        DecodeRLE8(ctx);
    }
    else if (hdr.compression == BI_RLE4)
    {
        RLEContext ctx(srcPixels.Begin(), pixelDataSize, dstPixels, dstStride,
                       hdr.width, hdr.height, hdr.topDown, palette.Begin(),
                       is3BytePalette, paletteEntries);
        DecodeRLE4(ctx);
    }
    else
    {
        for (CKDWORD y = 0; y < hdr.height; y++)
        {
            CKDWORD srcY = hdr.topDown ? y : (hdr.height - 1 - y);
            const XBYTE *srcRow = srcPixels.Begin() + srcY * srcStride;
            XBYTE *dstRow = dstPixels + y * dstStride;

            switch (hdr.bitCount)
            {
            case 1:
                DecodeRow1bpp(srcRow, dstRow, hdr.width, palette.Begin(), is3BytePalette, paletteEntries);
                break;
            case 4:
                DecodeRow4bpp(srcRow, dstRow, hdr.width, palette.Begin(), is3BytePalette, paletteEntries);
                break;
            case 8:
                DecodeRow8bpp(srcRow, dstRow, hdr.width, palette.Begin(), is3BytePalette, paletteEntries);
                break;
            case 16:
                DecodeRow16bpp(srcRow, dstRow, hdr.width, hdr.redMask, hdr.greenMask, hdr.blueMask, hdr.alphaMask, useMasks);
                break;
            case 24:
                DecodeRow24bpp(srcRow, dstRow, hdr.width);
                break;
            case 32:
                DecodeRow32bpp(srcRow, dstRow, hdr.width, hdr.redMask, hdr.greenMask, hdr.blueMask, hdr.alphaMask, useMasks);
                break;
            }
        }
    }

    // Fill properties
    ImageReader::FillFormatBGRA32(props->m_Format, (int)hdr.width, (int)hdr.height, (int)dstStride, dstPixels);
    props->m_Data = dstPixels;
    return 0;
}

//=============================================================================
// BMP_Save - Core Saving Function
//=============================================================================
int BMP_Save(void **outBuffer, CKBitmapProperties *props, int bitDepth)
{
    if (!props || !props->m_Format.Image)
        return 0;

    CKDWORD width = props->m_Format.Width;
    CKDWORD height = props->m_Format.Height;
    CKBYTE *srcPixels = props->m_Format.Image;
    CKDWORD srcStride = props->m_Format.BytesPerLine;

    if (width == 0 || height == 0)
        return 0;
    if (bitDepth != 8 && bitDepth != 9 && bitDepth != 24 && bitDepth != 32)
        bitDepth = 24;

    CKBOOL useRle8 = (bitDepth == 9);
    CKDWORD headerBitDepth = useRle8 ? 8 : (CKDWORD)bitDepth;
    CKDWORD dstStride = ((width * headerBitDepth + 31) / 32) * 4;
    CKDWORD paletteSize = (headerBitDepth == 8) ? 256 * 4 : 0;
    CKDWORD headerSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + paletteSize;

    // Handle RLE encoding
    XArray<CKBYTE> rleData;
    CKDWORD pixelDataSize = 0;

    if (useRle8)
    {
        XArray<CKBYTE> row((int)width);
        row.Resize((int)width);

        for (CKDWORD y = 0; y < height; y++)
        {
            CKBYTE *srcRow = srcPixels + (height - 1 - y) * srcStride;
            for (CKDWORD x = 0; x < width; x++)
                row.Begin()[x] = ToGray(srcRow[x * 4], srcRow[x * 4 + 1], srcRow[x * 4 + 2]);
            RLE8::EncodeRow(row.Begin(), width, rleData);
            RLE8::EmitEOL(rleData);
        }
        RLE8::EmitEOB(rleData);
        pixelDataSize = (CKDWORD)rleData.Size();
    }
    else
    {
        pixelDataSize = dstStride * height;
    }

    CKDWORD fileSize = headerSize + pixelDataSize;
    CKBYTE *buffer = new CKBYTE[fileSize];

    // File header
    BITMAPFILEHEADER *fh = (BITMAPFILEHEADER *)buffer;
    fh->bfType = 0x4D42;
    fh->bfSize = fileSize;
    fh->bfReserved1 = fh->bfReserved2 = 0;
    fh->bfOffBits = headerSize;

    // Info header
    BITMAPINFOHEADER *ih = (BITMAPINFOHEADER *)(buffer + sizeof(BITMAPFILEHEADER));
    ih->biSize = sizeof(BITMAPINFOHEADER);
    ih->biWidth = width;
    ih->biHeight = height;
    ih->biPlanes = 1;
    ih->biBitCount = (CKWORD)headerBitDepth;
    ih->biCompression = useRle8 ? BI_RLE8 : BI_RGB;
    ih->biSizeImage = pixelDataSize;
    ih->biXPelsPerMeter = ih->biYPelsPerMeter = 2835;
    ih->biClrUsed = (headerBitDepth == 8) ? 256 : 0;
    ih->biClrImportant = 0;

    // Palette
    CKBYTE *palPtr = buffer + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    if (headerBitDepth == 8)
    {
        for (int i = 0; i < 256; i++)
        {
            palPtr[i * 4 + 0] = palPtr[i * 4 + 1] = palPtr[i * 4 + 2] = (CKBYTE)i;
            palPtr[i * 4 + 3] = 0;
        }
    }

    // Pixel data
    CKBYTE *dstData = buffer + headerSize;
    if (useRle8)
    {
        memcpy(dstData, rleData.Begin(), pixelDataSize);
    }
    else
    {
        for (CKDWORD y = 0; y < height; y++)
        {
            CKBYTE *srcRow = srcPixels + (height - 1 - y) * srcStride;
            CKBYTE *dstRow = dstData + y * dstStride;

            if (headerBitDepth == 8)
            {
                for (CKDWORD x = 0; x < width; x++)
                    dstRow[x] = ToGray(srcRow[x * 4], srcRow[x * 4 + 1], srcRow[x * 4 + 2]);
            }
            else if (headerBitDepth == 24)
            {
                for (CKDWORD x = 0; x < width; x++)
                {
                    dstRow[x * 3 + 0] = srcRow[x * 4 + 0];
                    dstRow[x * 3 + 1] = srcRow[x * 4 + 1];
                    dstRow[x * 3 + 2] = srcRow[x * 4 + 2];
                }
            }
            else
            {
                memcpy(dstRow, srcRow, width * 4);
            }
        }
    }

    // Output
    if (*outBuffer)
    {
        FILE *fp = fopen((const char *)*outBuffer, "wb");
        if (!fp)
        {
            delete[] buffer;
            return 0;
        }
        fwrite(buffer, 1, fileSize, fp);
        fclose(fp);
        delete[] buffer;
    }
    else
    {
        *outBuffer = buffer;
    }
    return (int)fileSize;
}
