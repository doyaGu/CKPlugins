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
        if (m_offset > m_size || size > m_size - m_offset)
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
        if (offset < 0)
        {
            const unsigned long long distance = -(static_cast<long long>(offset));
            if (distance > m_offset)
                return FALSE;
            m_offset -= static_cast<CKDWORD>(distance);
        }
        else
        {
            const CKDWORD distance = static_cast<CKDWORD>(offset);
            if (m_offset > m_size || distance > m_size - m_offset)
                return FALSE;
            m_offset += distance;
        }
        return TRUE;
    }
    CKDWORD Tell() const override { return m_offset; }
    CKDWORD Size() const override { return m_size; }
    CKBOOL ReadAt(CKDWORD offset, void *buffer, CKDWORD size) override
    {
        if (offset > m_size || size > m_size - offset)
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
    CKBOOL Delta(CKBYTE dx, CKBYTE dy)
    {
        if (x > width || dx > width - x)
            return FALSE;

        if (y >= height)
            return FALSE;

        // A delta that leaves the image skips every remaining row, which
        // ends decoding just like running past the last line.
        if (topDown ? (dy >= height - y) : (dy > y))
        {
            y = height;
            return TRUE;
        }
        y = topDown ? y + dy : y - dy;
        x += dx;
        return TRUE;
    }
    CKBOOL HasMore() const { return srcPos < srcSize && y < height; }
    CKBOOL CanRead(CKDWORD count) const
    {
        return srcPos <= srcSize && count <= srcSize - srcPos;
    }
    CKBOOL TryReadByte(CKBYTE &value)
    {
        if (!CanRead(1))
            return FALSE;
        value = src[srcPos++];
        return TRUE;
    }
    void SetPixel(CKBYTE idx)
    {
        XBYTE *row = Row();
        if (row && x < width)
            SetBGRAFromPalette(row, x++, idx, palette, is3BytePalette, paletteEntries);
    }
};

static CKBOOL DecodeRLE8(RLEContext &ctx)
{
    while (ctx.HasMore())
    {
        CKBYTE first = 0;
        CKBYTE second = 0;
        if (!ctx.TryReadByte(first) || !ctx.TryReadByte(second))
            return FALSE;

        if (first == 0)
        {
            if (second == 0)
                ctx.NextLine();
            else if (second == 1)
                return TRUE;
            else if (second == 2)
            {
                CKBYTE dx = 0;
                CKBYTE dy = 0;
                if (!ctx.TryReadByte(dx) || !ctx.TryReadByte(dy) || !ctx.Delta(dx, dy))
                    return FALSE;
            }
            else
            {
                const CKDWORD dataSize = (static_cast<CKDWORD>(second) + 1U) & ~1U;
                if (!ctx.CanRead(dataSize))
                    return FALSE;
                for (CKDWORD i = 0; i < second; i++)
                    ctx.SetPixel(ctx.src[ctx.srcPos + i]);
                ctx.srcPos += dataSize;
            }
        }
        else
        {
            for (CKDWORD i = 0; i < first; i++)
                ctx.SetPixel(second);
        }
    }
    return TRUE;
}

static CKBOOL DecodeRLE4(RLEContext &ctx)
{
    while (ctx.HasMore())
    {
        CKBYTE first = 0;
        CKBYTE second = 0;
        if (!ctx.TryReadByte(first) || !ctx.TryReadByte(second))
            return FALSE;

        if (first == 0)
        {
            if (second == 0)
                ctx.NextLine();
            else if (second == 1)
                return TRUE;
            else if (second == 2)
            {
                CKBYTE dx = 0;
                CKBYTE dy = 0;
                if (!ctx.TryReadByte(dx) || !ctx.TryReadByte(dy) || !ctx.Delta(dx, dy))
                    return FALSE;
            }
            else
            {
                const CKDWORD packedSize = (static_cast<CKDWORD>(second) + 1U) / 2U;
                const CKDWORD dataSize = (packedSize + 1U) & ~1U;
                if (!ctx.CanRead(dataSize))
                    return FALSE;
                for (CKDWORD i = 0; i < second; i++)
                {
                    const CKBYTE packed = ctx.src[ctx.srcPos + i / 2U];
                    const CKBYTE idx = ((i & 1U) == 0) ? (packed >> 4) : (packed & 0x0F);
                    ctx.SetPixel(idx);
                }
                ctx.srcPos += dataSize;
            }
        }
        else
        {
            CKBYTE idx1 = second >> 4, idx2 = second & 0x0F;
            for (CKDWORD i = 0; i < first; i++)
                ctx.SetPixel((i & 1) ? idx2 : idx1);
        }
    }
    return TRUE;
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
            dst[x * 4 + 3] = src[x * 4 + 3];
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
    CKDWORD compression, colorsUsed, headerSize, imageSize;
    CKBOOL topDown;
    CKDWORD redMask, greenMask, blueMask, alphaMask;
    CKDWORD pixelDataOffset;

    BmpHeader() : width(0), height(0), bitCount(0), planes(0), compression(0),
                  colorsUsed(0), headerSize(0), imageSize(0), topDown(FALSE),
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
        BITMAPINFOHEADER info = {};
        info.biSize = hdr.headerSize;
        CKDWORD toRead = (hdr.headerSize > sizeof(info)) ? sizeof(info) - 4 : hdr.headerSize - 4;
        if (!src.Read(&info.biWidth, toRead))
            return CKBITMAPERROR_READERROR;
        if (hdr.headerSize > sizeof(info))
        {
            const CKDWORD extraHeaderSize = hdr.headerSize - sizeof(info);
            const CKDWORD position = src.Tell();
            if (position > src.Size() || extraHeaderSize > src.Size() - position ||
                !src.Seek(position + extraHeaderSize))
                return CKBITMAPERROR_READERROR;
        }

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
        hdr.imageSize = info.biSizeImage;

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
            if (!src.ReadAt(base, &hdr.redMask, 4) ||
                !src.ReadAt(base + 4, &hdr.greenMask, 4) ||
                !src.ReadAt(base + 8, &hdr.blueMask, 4) ||
                (hdr.headerSize >= 56 && !src.ReadAt(base + 12, &hdr.alphaMask, 4)))
                return CKBITMAPERROR_READERROR;
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

// Exact palettes for up to 256 RGB colors; weighted median cut above that.
// Quantization storage is bounded independently of image dimensions.
struct BmpSaveColorBin
{
    uint64_t Count;
    double Sum[3]; // R, G, B
};

struct BmpSaveColorBox
{
    int Min[3], Max[3];
    uint64_t Count;
};

class BmpSavePalette
{
public:
    BmpSavePalette() : m_Count(0), m_Reduced(FALSE)
    {
        memset(m_Colors, 0, sizeof(m_Colors));
        memset(m_Hash, 0, sizeof(m_Hash));
    }

    void Build(const CKBYTE *pixels, CKDWORD width, CKDWORD height, CKDWORD stride)
    {
        for (CKDWORD y = 0; y < height; y++)
        {
            const CKBYTE *row = pixels + (size_t)y * stride;
            for (CKDWORD x = 0; x < width; x++)
            {
                const CKBYTE *pixel = row + (size_t)x * 4;
                unsigned slot = FindSlot(pixel);
                if (m_Hash[slot])
                    continue;
                if (m_Count == 256)
                {
                    Reduce(pixels, width, height, stride);
                    return;
                }
                memcpy(m_Colors + m_Count * 4, pixel, 3);
                m_Hash[slot] = (CKWORD)(++m_Count);
            }
        }
    }

    CKBYTE Index(const CKBYTE *pixel) const
    {
        if (m_Reduced)
            return m_Map[BinIndex(pixel)];
        return (CKBYTE)(m_Hash[FindSlot(pixel)] - 1);
    }

    const CKBYTE *Colors() const { return m_Colors; }

private:
    unsigned FindSlot(const CKBYTE *pixel) const
    {
        unsigned rgb = ((unsigned)pixel[2] << 16) | ((unsigned)pixel[1] << 8) | pixel[0];
        unsigned slot = (rgb * 2654435761u) >> 22;
        while (m_Hash[slot])
        {
            const CKBYTE *color = m_Colors + (m_Hash[slot] - 1) * 4;
            if (color[0] == pixel[0] && color[1] == pixel[1] && color[2] == pixel[2])
                break;
            slot = (slot + 1) & 1023;
        }
        return slot;
    }

    static unsigned BinIndex(const CKBYTE *pixel)
    {
        return ((unsigned)(pixel[2] >> 3) << 10) | ((unsigned)(pixel[1] >> 3) << 5) | (pixel[0] >> 3);
    }

    static void Measure(BmpSaveColorBox &box, const BmpSaveColorBin *bins)
    {
        int low[] = {31, 31, 31}, high[] = {0, 0, 0};
        box.Count = 0;
        for (int r = box.Min[0]; r <= box.Max[0]; r++)
            for (int g = box.Min[1]; g <= box.Max[1]; g++)
                for (int b = box.Min[2]; b <= box.Max[2]; b++)
                {
                    const uint64_t count = bins[(r << 10) | (g << 5) | b].Count;
                    if (!count)
                        continue;
                    box.Count += count;
                    const int value[] = {r, g, b};
                    for (int c = 0; c < 3; c++)
                    {
                        if (value[c] < low[c]) low[c] = value[c];
                        if (value[c] > high[c]) high[c] = value[c];
                    }
                }
        memcpy(box.Min, low, sizeof(low));
        memcpy(box.Max, high, sizeof(high));
    }

    void Reduce(const CKBYTE *pixels, CKDWORD width, CKDWORD height, CKDWORD stride)
    {
        XArray<BmpSaveColorBin> histogram;
        histogram.Resize(32768);
        BmpSaveColorBin *bins = histogram.Begin();
        memset(bins, 0, 32768 * sizeof(BmpSaveColorBin));
        for (CKDWORD y = 0; y < height; y++)
        {
            const CKBYTE *row = pixels + (size_t)y * stride;
            for (CKDWORD x = 0; x < width; x++)
            {
                const CKBYTE *pixel = row + (size_t)x * 4;
                BmpSaveColorBin &bin = bins[BinIndex(pixel)];
                ++bin.Count;
                for (int c = 0; c < 3; c++)
                    bin.Sum[c] += pixel[2 - c];
            }
        }

        BmpSaveColorBox boxes[256];
        for (int c = 0; c < 3; c++) { boxes[0].Min[c] = 0; boxes[0].Max[c] = 31; }
        Measure(boxes[0], bins);
        m_Count = 1;
        while (m_Count < 256)
        {
            int selected = -1, axis = 0;
            double best = 0;
            for (int i = 0; i < m_Count; i++)
                for (int c = 0; c < 3; c++)
                {
                    double score = (double)boxes[i].Count * (boxes[i].Max[c] - boxes[i].Min[c]);
                    if (score > best) { selected = i; axis = c; best = score; }
                }
            if (selected < 0)
                break;
            BmpSaveColorBox &left = boxes[selected];
            uint64_t weights[32] = {};
            for (int r = left.Min[0]; r <= left.Max[0]; r++)
                for (int g = left.Min[1]; g <= left.Max[1]; g++)
                    for (int b = left.Min[2]; b <= left.Max[2]; b++)
                    {
                        const int value[] = {r, g, b};
                        weights[value[axis]] += bins[(r << 10) | (g << 5) | b].Count;
                    }
            uint64_t cumulative = 0;
            int cut = left.Min[axis];
            for (; cut < left.Max[axis]; cut++)
            {
                cumulative += weights[cut];
                if (cumulative >= left.Count / 2 + left.Count % 2)
                    break;
            }
            if (cut == left.Max[axis])
                --cut;
            boxes[m_Count] = left;
            left.Max[axis] = cut;
            boxes[m_Count].Min[axis] = cut + 1;
            Measure(left, bins);
            Measure(boxes[m_Count], bins);
            ++m_Count;
        }

        memset(m_Colors, 0, sizeof(m_Colors));
        memset(m_Map, 0, sizeof(m_Map));
        for (int i = 0; i < m_Count; i++)
        {
            const BmpSaveColorBox &box = boxes[i];
            double sum[3] = {};
            for (int r = box.Min[0]; r <= box.Max[0]; r++)
                for (int g = box.Min[1]; g <= box.Max[1]; g++)
                    for (int b = box.Min[2]; b <= box.Max[2]; b++)
                    {
                        unsigned index = (r << 10) | (g << 5) | b;
                        m_Map[index] = (CKBYTE)i;
                        for (int c = 0; c < 3; c++) sum[c] += bins[index].Sum[c];
                    }
            for (int c = 0; c < 3; c++)
                m_Colors[i * 4 + 2 - c] = (CKBYTE)(sum[c] / (double)box.Count + 0.5);
        }
        m_Reduced = TRUE;
    }

    int m_Count;
    CKBOOL m_Reduced;
    CKBYTE m_Colors[1024]; // BMP B,G,R,reserved entries; reserved stays zero.
    CKWORD m_Hash[1024];
    CKBYTE m_Map[32768];
};

//=============================================================================
// RLE8 Encoding (for save)
//=============================================================================
static CKBOOL BmpEncodeRle8(const CKBYTE *pixels, CKDWORD width, CKDWORD height,
                           CKDWORD stride, const BmpSavePalette &palette, ImageWriterBuffer &out)
{
    for (CKDWORD y = 0; y < height; y++)
    {
        const CKBYTE *row = pixels + (size_t)(height - 1 - y) * stride;
        CKDWORD x = 0;
        while (x < width)
        {
            CKBYTE v = palette.Index(row + (size_t)x * 4);
            CKDWORD runLen = 1;
            while (x + runLen < width && runLen < 255 && palette.Index(row + (size_t)(x + runLen) * 4) == v)
                runLen++;

            if (runLen >= 2)
            {
                const CKBYTE packet[] = {(CKBYTE)runLen, v};
                if (!out.Append(packet, sizeof(packet)))
                    return FALSE;
                x += runLen;
                continue;
            }

            CKDWORD litLen = 1;
            while (x + litLen < width && litLen < 255)
            {
                if (x + litLen + 1 < width &&
                    palette.Index(row + (size_t)(x + litLen) * 4) == palette.Index(row + (size_t)(x + litLen + 1) * 4))
                    break;
                litLen++;
            }

            if (litLen >= 3)
            {
                CKBYTE packet[258];
                packet[0] = 0;
                packet[1] = (CKBYTE)litLen;
                for (CKDWORD i = 0; i < litLen; i++)
                    packet[2 + i] = palette.Index(row + (size_t)(x + i) * 4);
                packet[2 + litLen] = 0;
                if (!out.Append(packet, 2 + litLen + (litLen & 1)))
                    return FALSE;
            }
            else
                for (CKDWORD i = 0; i < litLen; i++)
                {
                    const CKBYTE packet[] = {1, palette.Index(row + (size_t)(x + i) * 4)};
                    if (!out.Append(packet, sizeof(packet)))
                        return FALSE;
                }
            x += litLen;
        }
        const CKBYTE eol[] = {0, 0};
        if (!out.Append(eol, sizeof(eol)))
            return FALSE;
    }
    const CKBYTE eob[] = {0, 1};
    return out.Append(eob, sizeof(eob));
}

//=============================================================================
// BmpReader Class Implementation
//=============================================================================
BmpReader::BmpReader() : ImageReader()
{
    m_Properties.Init(BMPREADER_GUID, "bmp");
    m_DefaultProperties.Init(BMPREADER_GUID, "bmp");
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
    if (!bp || bp->m_Size < sizeof(BmpBitmapProperties))
        return FALSE;
    return static_cast<BmpBitmapProperties *>(bp)->m_BitDepth == 32;
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

void BmpReader::GetBitmapDefaultProperties(CKBitmapProperties **bp)
{
    if (bp)
        *bp = &m_DefaultProperties;
}

void BmpReader::SetBitmapDefaultProperties(CKBitmapProperties *bp)
{
    if (bp && bp->m_Size >= sizeof(BmpBitmapProperties))
        m_DefaultProperties.m_BitDepth = static_cast<BmpBitmapProperties *>(bp)->m_BitDepth;
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
    if (hdr.pixelDataOffset < src->Tell() || hdr.pixelDataOffset > src->Size() ||
        !src->Seek(hdr.pixelDataOffset))
    {
        delete src;
        return CKBITMAPERROR_FILECORRUPTED;
    }

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
    const CKDWORD availablePixelData = src->Size() - src->Tell();
    if (hdr.compression == BI_RGB || hdr.compression == BI_BITFIELDS || hdr.compression == BI_ALPHABITFIELDS)
    {
        unsigned long long total = (unsigned long long)srcStride * hdr.height;
        if (total > 0xFFFFFFFFULL || total > availablePixelData)
        {
            delete src;
            return CKBITMAPERROR_FILECORRUPTED;
        }
        pixelDataSize = (CKDWORD)total;
    }
    else
    {
        // Some writers store the uncompressed size in biSizeImage for RLE
        // data, so only trust it when it fits in the remaining bytes.
        pixelDataSize = (hdr.imageSize && hdr.imageSize <= availablePixelData)
                            ? hdr.imageSize
                            : availablePixelData;
    }

    if (pixelDataSize > static_cast<CKDWORD>(INT_MAX))
    {
        delete src;
        return CKBITMAPERROR_FILECORRUPTED;
    }

    // Read pixel data
    XArray<XBYTE> srcPixels;
    srcPixels.Resize((int)pixelDataSize);
    if (pixelDataSize > 0 && !src->Read(srcPixels.Begin(), pixelDataSize))
    {
        delete src;
        return CKBITMAPERROR_READERROR;
    }
    delete src;
    src = NULL;

    // Allocate destination
    const unsigned long long dstStride64 = (unsigned long long)hdr.width * 4ULL;
    if (dstStride64 > 0xFFFFFFFFULL)
        return CKBITMAPERROR_FILECORRUPTED;
    CKDWORD dstStride = static_cast<CKDWORD>(dstStride64);
    unsigned long long dstTotal = dstStride64 * hdr.height;
    if (dstTotal > 0xFFFFFFFFULL)
        return CKBITMAPERROR_FILECORRUPTED;

    XBYTE *dstPixels = new XBYTE[(CKDWORD)dstTotal];
    if (hdr.compression == BI_RLE4 || hdr.compression == BI_RLE8)
    {
        // RLE skips leave palette index zero, including early end and delta holes.
        for (CKDWORD pixel = 0; pixel < hdr.width * hdr.height; ++pixel)
            SetBGRAFromPalette(dstPixels, pixel, 0, palette.Begin(), is3BytePalette, paletteEntries);
    }
    else
    {
        memset(dstPixels, 0xFF, (CKDWORD)dstTotal);
    }

    // Decode
    CKBOOL useMasks = (hdr.compression == BI_BITFIELDS || hdr.compression == BI_ALPHABITFIELDS);

    if (hdr.compression == BI_RLE8)
    {
        RLEContext ctx(srcPixels.Begin(), pixelDataSize, dstPixels, dstStride,
                       hdr.width, hdr.height, hdr.topDown, palette.Begin(),
                       is3BytePalette, paletteEntries);
        if (!DecodeRLE8(ctx))
        {
            delete[] dstPixels;
            return CKBITMAPERROR_FILECORRUPTED;
        }
    }
    else if (hdr.compression == BI_RLE4)
    {
        RLEContext ctx(srcPixels.Begin(), pixelDataSize, dstPixels, dstStride,
                       hdr.width, hdr.height, hdr.topDown, palette.Begin(),
                       is3BytePalette, paletteEntries);
        if (!DecodeRLE4(ctx))
        {
            delete[] dstPixels;
            return CKBITMAPERROR_FILECORRUPTED;
        }
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
    ImageReader::FreeBitmapData(props);
    ImageReader::FillFormatBGRA32(props->m_Format, (int)hdr.width, (int)hdr.height, (int)dstStride, dstPixels);
    props->m_Data = dstPixels;
    return 0;
}

//=============================================================================
// BMP_Save - Core Saving Function
//=============================================================================
int BMP_Save(void **outBuffer, CKBitmapProperties *props, int bitDepth)
{
    if (!props || props->m_Format.Width <= 0 || props->m_Format.Height <= 0)
        return 0;

    CKDWORD width = props->m_Format.Width;
    CKDWORD height = props->m_Format.Height;
    if (bitDepth != 8 && bitDepth != 9 && bitDepth != 16 && bitDepth != 24 && bitDepth != 32)
        bitDepth = 24;

    CKBOOL useRle8 = (bitDepth == 9);
    CKDWORD headerBitDepth = useRle8 ? 8 : (CKDWORD)bitDepth;
    uint64_t dstStride = (((uint64_t)width * headerBitDepth + 31) / 32) * 4;
    CKDWORD paletteSize = (headerBitDepth == 8) ? 256 * 4 : 0;
    CKDWORD headerSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + paletteSize;

    uint64_t pixelDataSize = dstStride * height;
    if (!useRle8 && pixelDataSize > (uint64_t)INT_MAX - headerSize)
        return 0;

    XArray<CKBYTE> converted;
    CKDWORD srcStride = 0;
    CKBYTE *srcPixels = ImageReader::GetSavePixels(props, converted, srcStride);
    if (!srcPixels)
        return 0;
    BmpSavePalette palette;
    if (headerBitDepth == 8)
        palette.Build(srcPixels, width, height, srcStride);
    if (useRle8)
    {
        ImageWriterBuffer count(NULL, INT_MAX - headerSize);
        if (!BmpEncodeRle8(srcPixels, width, height, srcStride, palette, count))
            return 0;
        pixelDataSize = count.Size();
    }

    int fileSize = (int)(headerSize + pixelDataSize);
    CKBYTE *buffer = new CKBYTE[fileSize]();

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
    ih->biSizeImage = (CKDWORD)pixelDataSize;
    ih->biXPelsPerMeter = ih->biYPelsPerMeter = 2835;
    ih->biClrUsed = (headerBitDepth == 8) ? 256 : 0;
    ih->biClrImportant = 0;

    // Palette
    CKBYTE *palPtr = buffer + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    if (headerBitDepth == 8)
        memcpy(palPtr, palette.Colors(), paletteSize);

    // Pixel data
    CKBYTE *dstData = buffer + headerSize;
    if (useRle8)
    {
        ImageWriterBuffer encoded(dstData, (size_t)pixelDataSize);
        if (!BmpEncodeRle8(srcPixels, width, height, srcStride, palette, encoded))
        {
            delete[] buffer;
            return 0;
        }
    }
    else
    {
        for (CKDWORD y = 0; y < height; y++)
        {
            CKBYTE *srcRow = srcPixels + (size_t)(height - 1 - y) * srcStride;
            CKBYTE *dstRow = dstData + (size_t)y * (size_t)dstStride;

            if (headerBitDepth == 8)
            {
                for (CKDWORD x = 0; x < width; x++)
                    dstRow[x] = palette.Index(srcRow + (size_t)x * 4);
            }
            else if (headerBitDepth == 16)
            {
                for (CKDWORD x = 0; x < width; x++)
                {
                    CKWORD pixel = ImageReader::PackRGB555(srcRow[x * 4], srcRow[x * 4 + 1], srcRow[x * 4 + 2]);
                    dstRow[x * 2 + 0] = (CKBYTE)(pixel & 0xFF);
                    dstRow[x * 2 + 1] = (CKBYTE)(pixel >> 8);
                }
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
        const size_t written = fwrite(buffer, 1, fileSize, fp);
        const int closed = fclose(fp);
        delete[] buffer;
        if (written != fileSize || closed != 0)
            return 0;
    }
    else
    {
        *outBuffer = buffer;
    }
    return (int)fileSize;
}
