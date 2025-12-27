#include "TgaReader.h"

#include "XArray.h"

//=============================================================================
// Data Source Abstraction
//=============================================================================
class TgaDataSource
{
public:
    virtual ~TgaDataSource() {}
    virtual CKBOOL Read(void *buffer, CKDWORD size) = 0;
    virtual CKBOOL Skip(CKDWORD bytes) = 0;
    virtual CKDWORD Tell() const = 0;
    virtual CKDWORD Remaining() const = 0;
    virtual CKBOOL ReadRemaining(XArray<CKBYTE> &out) = 0;
};

class TgaFileSource : public TgaDataSource
{
    FILE *m_fp;
    CKDWORD m_size;

public:
    TgaFileSource(const char *filename) : m_fp(NULL), m_size(0)
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
    ~TgaFileSource()
    {
        if (m_fp)
            fclose(m_fp);
    }
    CKBOOL IsValid() const { return m_fp != NULL; }

    CKBOOL Read(void *buffer, CKDWORD size) override
    {
        return m_fp && fread(buffer, 1, size, m_fp) == size;
    }
    CKBOOL Skip(CKDWORD bytes) override
    {
        return m_fp && fseek(m_fp, (long)bytes, SEEK_CUR) == 0;
    }
    CKDWORD Tell() const override
    {
        if (!m_fp)
            return 0;
        long pos = ftell(m_fp);
        return (pos >= 0) ? (CKDWORD)pos : 0;
    }
    CKDWORD Remaining() const override
    {
        CKDWORD pos = Tell();
        return (pos < m_size) ? (m_size - pos) : 0;
    }
    CKBOOL ReadRemaining(XArray<CKBYTE> &out) override
    {
        CKDWORD rem = Remaining();
        if (rem == 0)
            return TRUE;
        out.Resize((int)rem);
        return Read(out.Begin(), rem);
    }
};

class TgaMemorySource : public TgaDataSource
{
    const CKBYTE *m_data;
    CKDWORD m_size;
    CKDWORD m_offset;

public:
    TgaMemorySource(const void *data, int size)
        : m_data((const CKBYTE *)data), m_size(size > 0 ? (CKDWORD)size : 0), m_offset(0) {}

    CKBOOL Read(void *buffer, CKDWORD size) override
    {
        if (m_offset + size > m_size)
            return FALSE;
        memcpy(buffer, m_data + m_offset, size);
        m_offset += size;
        return TRUE;
    }
    CKBOOL Skip(CKDWORD bytes) override
    {
        if (m_offset + bytes > m_size)
            return FALSE;
        m_offset += bytes;
        return TRUE;
    }
    CKDWORD Tell() const override { return m_offset; }
    CKDWORD Remaining() const override { return (m_offset < m_size) ? (m_size - m_offset) : 0; }
    CKBOOL ReadRemaining(XArray<CKBYTE> &out) override
    {
        CKDWORD rem = Remaining();
        if (rem == 0)
            return TRUE;
        out.Resize((int)rem);
        memcpy(out.Begin(), m_data + m_offset, rem);
        m_offset += rem;
        return TRUE;
    }
};

//=============================================================================
// Safe Arithmetic
//=============================================================================
static CKBOOL SafeMul32(CKDWORD a, CKDWORD b, CKDWORD &out)
{
    unsigned long long v = (unsigned long long)a * b;
    if (v > 0xFFFFFFFFULL)
        return FALSE;
    out = (CKDWORD)v;
    return TRUE;
}

//=============================================================================
// Coordinate Mapping
//=============================================================================
static CKDWORD DeinterleaveY(CKDWORD fileY, CKDWORD height, CKBYTE mode)
{
    if (mode == 0)
        return fileY;
    if (mode == 1)
    {
        CKDWORD evenCount = (height + 1) / 2;
        return (fileY < evenCount) ? (fileY * 2) : ((fileY - evenCount) * 2 + 1);
    }
    // mode 2: four-way
    CKDWORD c0 = (height + 3) / 4, c1 = (height + 2) / 4, c2 = (height + 1) / 4;
    if (fileY < c0)
        return fileY * 4;
    fileY -= c0;
    if (fileY < c1)
        return fileY * 4 + 1;
    fileY -= c1;
    if (fileY < c2)
        return fileY * 4 + 2;
    return (fileY - c2) * 4 + 3;
}

static CKDWORD MapX(CKDWORD x, CKDWORD w, CKBOOL rtl) { return rtl ? (w - 1 - x) : x; }
static CKDWORD MapY(CKDWORD y, CKDWORD h, CKBOOL td, CKBYTE interleave)
{
    CKDWORD logical = DeinterleaveY(y, h, interleave);
    return td ? logical : (h - 1 - logical);
}

//=============================================================================
// Pixel Decoding
//=============================================================================
static CKWORD ReadLE16(const CKBYTE *p) { return (CKWORD)(p[0] | (p[1] << 8)); }

static void Decode15or16(CKWORD c, CKBYTE alphaBits, CKBYTE out[4])
{
    out[0] = (CKBYTE)(((c >> 0) & 0x1F) * 255 / 31);
    out[1] = (CKBYTE)(((c >> 5) & 0x1F) * 255 / 31);
    out[2] = (CKBYTE)(((c >> 10) & 0x1F) * 255 / 31);
    out[3] = (alphaBits > 0) ? ((c & 0x8000) ? 255 : 0) : 255;
}

static void DecodePaletteEntry(const TGAHEADER *hdr, CKBYTE alphaBits,
                               const CKBYTE *colorMap, CKDWORD entries, CKDWORD bytesPerEntry,
                               CKWORD index, CKBYTE out[4])
{
    out[0] = out[1] = out[2] = 0;
    out[3] = 255;
    if (!hdr || !colorMap)
        return;

    int idx = (int)index - (int)hdr->colorMapOrigin;
    if (idx < 0 || (CKDWORD)idx >= entries)
        return;

    const CKBYTE *e = colorMap + idx * bytesPerEntry;
    switch (hdr->colorMapDepth)
    {
    case 15:
    case 16:
        Decode15or16(ReadLE16(e), (hdr->colorMapDepth == 16) ? alphaBits : 0, out);
        break;
    case 24:
        out[0] = e[0];
        out[1] = e[1];
        out[2] = e[2];
        out[3] = 255;
        break;
    case 32:
        out[0] = e[0];
        out[1] = e[1];
        out[2] = e[2];
        out[3] = e[3];
        break;
    }
}

static void DecodePixel(const TGAHEADER *hdr, CKDWORD depth, CKBOOL hasColorMap, CKBOOL isGray,
                        CKBYTE alphaBits, const CKBYTE *colorMap, CKDWORD cmEntries, CKDWORD cmBytes,
                        const CKBYTE *src, CKBYTE out[4])
{
    out[0] = out[1] = out[2] = 0;
    out[3] = 255;
    if (!src)
        return;

    if (hasColorMap)
    {
        CKWORD idx = (depth == 8) ? src[0] : ReadLE16(src);
        DecodePaletteEntry(hdr, alphaBits, colorMap, cmEntries, cmBytes, idx, out);
        return;
    }

    if (isGray)
    {
        out[0] = out[1] = out[2] = src[0];
        out[3] = (depth == 16) ? src[1] : 255;
        return;
    }

    switch (depth)
    {
    case 15:
    case 16:
        Decode15or16(ReadLE16(src), (depth == 16) ? alphaBits : 0, out);
        break;
    case 24:
        out[0] = src[0];
        out[1] = src[1];
        out[2] = src[2];
        out[3] = 255;
        break;
    case 32:
        out[0] = src[0];
        out[1] = src[1];
        out[2] = src[2];
        out[3] = src[3];
        break;
    }
}

static CKBOOL OutputHasAlpha(const TGAHEADER *hdr, CKDWORD depth, CKBOOL hasColorMap, CKBOOL isGray, CKBYTE alphaBits)
{
    if (hasColorMap)
        return hdr && (hdr->colorMapDepth == 32 || (hdr->colorMapDepth == 16 && alphaBits > 0));
    if (isGray)
        return depth == 16;
    return depth == 32 || (depth == 16 && alphaBits > 0);
}

//=============================================================================
// TGA Header Parsing
//=============================================================================
struct TgaContext
{
    TGAHEADER header;
    CKDWORD width, height, pixelDepth, srcBytesPerPixel;
    CKBYTE alphaBits, interleaveMode;
    CKBOOL isRLE, hasColorMap, isGrayscale;
    CKBOOL isRightToLeft, isTopDown;
    XArray<CKBYTE> colorMap;
    CKDWORD colorMapEntries, colorMapBytesPerEntry;

    TgaContext() : width(0), height(0), pixelDepth(0), srcBytesPerPixel(0),
                   alphaBits(0), interleaveMode(0), isRLE(FALSE), hasColorMap(FALSE),
                   isGrayscale(FALSE), isRightToLeft(FALSE), isTopDown(FALSE),
                   colorMapEntries(0), colorMapBytesPerEntry(0)
    {
        memset(&header, 0, sizeof(header));
    }
};

static int ParseTgaHeader(TgaDataSource &src, TgaContext &ctx)
{
    if (!src.Read(&ctx.header, sizeof(ctx.header)))
        return CKBITMAPERROR_READERROR;

    switch (ctx.header.imageType)
    {
    case TGA_TYPE_COLORMAP:
        ctx.hasColorMap = TRUE;
        break;
    case TGA_TYPE_TRUECOLOR:
        break;
    case TGA_TYPE_GRAYSCALE:
        ctx.isGrayscale = TRUE;
        break;
    case TGA_TYPE_RLE_COLORMAP:
        ctx.hasColorMap = TRUE;
        ctx.isRLE = TRUE;
        break;
    case TGA_TYPE_RLE_TRUECOLOR:
        ctx.isRLE = TRUE;
        break;
    case TGA_TYPE_RLE_GRAYSCALE:
        ctx.isGrayscale = TRUE;
        ctx.isRLE = TRUE;
        break;
    default:
        return CKBITMAPERROR_UNSUPPORTEDFILE;
    }

    ctx.width = ctx.header.width;
    ctx.height = ctx.header.height;
    ctx.pixelDepth = ctx.header.pixelDepth;
    ctx.alphaBits = ctx.header.imageDescriptor & 0x0F;
    ctx.interleaveMode = (ctx.header.imageDescriptor >> 6) & 0x03;
    ctx.isRightToLeft = (ctx.header.imageDescriptor & 0x10) != 0;
    ctx.isTopDown = (ctx.header.imageDescriptor & 0x20) != 0;

    if (ctx.width == 0 || ctx.height == 0)
        return CKBITMAPERROR_FILECORRUPTED;
    if (ctx.interleaveMode == 3)
        return CKBITMAPERROR_UNSUPPORTEDFILE;

    // Skip image ID
    if (ctx.header.idLength > 0 && !src.Skip(ctx.header.idLength))
        return CKBITMAPERROR_READERROR;

    // Read color map
    if (ctx.header.colorMapType == 1 && ctx.header.colorMapLength > 0)
    {
        ctx.colorMapEntries = ctx.header.colorMapLength;
        ctx.colorMapBytesPerEntry = (ctx.header.colorMapDepth + 7) / 8;
        if (ctx.colorMapBytesPerEntry == 0)
            return CKBITMAPERROR_FILECORRUPTED;

        CKDWORD cmSize;
        if (!SafeMul32(ctx.colorMapEntries, ctx.colorMapBytesPerEntry, cmSize))
            return CKBITMAPERROR_FILECORRUPTED;

        ctx.colorMap.Resize((int)cmSize);
        if (!src.Read(ctx.colorMap.Begin(), cmSize))
            return CKBITMAPERROR_READERROR;
    }

    // Validate color-mapped images
    if (ctx.hasColorMap)
    {
        if (ctx.header.colorMapType != 1 || !ctx.colorMap.Begin() ||
            ctx.colorMapEntries == 0 || ctx.colorMapBytesPerEntry == 0)
            return CKBITMAPERROR_FILECORRUPTED;
        if (ctx.pixelDepth != 8 && ctx.pixelDepth != 16)
            return CKBITMAPERROR_UNSUPPORTEDFILE;
        if (ctx.header.colorMapDepth != 15 && ctx.header.colorMapDepth != 16 &&
            ctx.header.colorMapDepth != 24 && ctx.header.colorMapDepth != 32)
            return CKBITMAPERROR_UNSUPPORTEDFILE;
    }
    else
    {
        if (ctx.isGrayscale)
        {
            if (ctx.pixelDepth != 8 && ctx.pixelDepth != 16)
                return CKBITMAPERROR_UNSUPPORTEDFILE;
        }
        else
        {
            if (ctx.pixelDepth != 15 && ctx.pixelDepth != 16 &&
                ctx.pixelDepth != 24 && ctx.pixelDepth != 32)
                return CKBITMAPERROR_UNSUPPORTEDFILE;
        }
    }

    ctx.srcBytesPerPixel = (ctx.pixelDepth + 7) / 8;
    return 0;
}

//=============================================================================
// TgaReader Class Implementation
//=============================================================================
TgaReader::TgaReader() : ImageReader()
{
    m_Properties.Init(TGAREADER_GUID, "tga");
}

TgaReader::~TgaReader()
{
    FreeBitmapData(&m_Properties);
}

CKPluginInfo *TgaReader::GetReaderInfo()
{
    return &g_PluginInfo[READER_INDEX_TGA];
}

int TgaReader::GetOptionsCount() { return 2; }

CKSTRING TgaReader::GetOptionDescription(int i)
{
    if (i == 0)
        return "Enum:Bit Depth:16 bit=16,24 bit=24,32 bit=32,Greyscale=64";
    if (i == 1)
        return "Boolean:Run Length Encoding";
    return "";
}

CKBOOL TgaReader::IsAlphaSaved(CKBitmapProperties *bp)
{
    if (!bp || bp->m_Size != 80)
        return FALSE;
    return ((CKDWORD *)bp)[18] == 32;
}

int TgaReader::ReadFile(CKSTRING filename, CKBitmapProperties **bp)
{
    if (!filename || !bp)
        return 1;
    int result = TGA_Read((void *)filename, 0, (CKBitmapProperties *)&m_Properties);
    *bp = (CKBitmapProperties *)&m_Properties;
    return result;
}

int TgaReader::ReadMemory(void *memory, int size, CKBitmapProperties **bp)
{
    if (!bp)
        return 1;
    int result = TGA_Read(memory, size, (CKBitmapProperties *)&m_Properties);
    *bp = (CKBitmapProperties *)&m_Properties;
    return result;
}

int TgaReader::SaveFile(CKSTRING filename, CKBitmapProperties *bp)
{
    if (!filename || !bp)
        return 0;
    TgaBitmapProperties local;
    memset(&local, 0, sizeof(local));
    memcpy(&local, bp, (bp->m_Size < sizeof(local)) ? bp->m_Size : sizeof(local));
    local.m_Size = sizeof(TgaBitmapProperties);
    if (bp->m_Size != sizeof(TgaBitmapProperties))
    {
        local.m_BitDepth = 24;
        local.m_UseRLE = 0;
    }
    void *ptr = (void *)filename;
    return TGA_Save(&ptr, (CKBitmapProperties *)&local, (int)local.m_BitDepth, (int)local.m_UseRLE);
}

int TgaReader::SaveMemory(void **memory, CKBitmapProperties *bp)
{
    if (!memory || !bp)
        return 0;
    TgaBitmapProperties local;
    memset(&local, 0, sizeof(local));
    memcpy(&local, bp, (bp->m_Size < sizeof(local)) ? bp->m_Size : sizeof(local));
    local.m_Size = sizeof(TgaBitmapProperties);
    if (bp->m_Size != sizeof(TgaBitmapProperties))
    {
        local.m_BitDepth = 24;
        local.m_UseRLE = 0;
    }
    *memory = NULL;
    return TGA_Save(memory, (CKBitmapProperties *)&local, (int)local.m_BitDepth, (int)local.m_UseRLE);
}

//=============================================================================
// TGA_Read - Core Reading Function
//=============================================================================
int TGA_Read(void *data, int size, CKBitmapProperties *props)
{
    if (!data || !props)
        return CKBITMAPERROR_GENERIC;
    if (size < 0)
        return CKBITMAPERROR_FILECORRUPTED;

    // Create data source
    TgaDataSource *src = (size == 0)
                             ? (TgaDataSource *)new TgaFileSource((const char *)data)
                             : (TgaDataSource *)new TgaMemorySource(data, size);

    if (size == 0 && !((TgaFileSource *)src)->IsValid())
    {
        delete src;
        return CKBITMAPERROR_READERROR;
    }

    TgaContext ctx;
    int result = ParseTgaHeader(*src, ctx);
    if (result != 0)
    {
        delete src;
        return result;
    }

    // Read pixel data
    XArray<CKBYTE> srcPixels;
    if (!src->ReadRemaining(srcPixels))
    {
        delete src;
        return CKBITMAPERROR_READERROR;
    }
    delete src;
    src = NULL;

    CKDWORD pixelDataSize = (CKDWORD)srcPixels.Size();

    // Allocate destination
    CKDWORD dstStride = ctx.width * 4;
    CKDWORD dstSize;
    if (!SafeMul32(dstStride, ctx.height, dstSize))
        return CKBITMAPERROR_FILECORRUPTED;

    CKBYTE *dstPixels = new CKBYTE[dstSize];
    memset(dstPixels, 0xFF, dstSize);

    // Decode pixels
    if (ctx.isRLE)
    {
        CKDWORD srcPos = 0;
        CKDWORD totalPixels = ctx.width * ctx.height;
        CKDWORD pixelCount = 0;

        while (pixelCount < totalPixels && srcPos < pixelDataSize)
        {
            CKBYTE packet = srcPixels.Begin()[srcPos++];
            CKDWORD count = (packet & 0x7F) + 1;

            if (packet & 0x80)
            {
                // RLE packet
                if (srcPos + ctx.srcBytesPerPixel > pixelDataSize)
                    break;
                CKBYTE pixel[4];
                DecodePixel(&ctx.header, ctx.pixelDepth, ctx.hasColorMap, ctx.isGrayscale,
                            ctx.alphaBits, ctx.colorMap.Begin(), ctx.colorMapEntries,
                            ctx.colorMapBytesPerEntry, srcPixels.Begin() + srcPos, pixel);
                srcPos += ctx.srcBytesPerPixel;

                for (CKDWORD i = 0; i < count && pixelCount < totalPixels; i++, pixelCount++)
                {
                    CKDWORD fx = pixelCount % ctx.width, fy = pixelCount / ctx.width;
                    CKDWORD dx = MapX(fx, ctx.width, ctx.isRightToLeft);
                    CKDWORD dy = MapY(fy, ctx.height, ctx.isTopDown, ctx.interleaveMode);
                    CKBYTE *dst = dstPixels + dy * dstStride + dx * 4;
                    dst[0] = pixel[0];
                    dst[1] = pixel[1];
                    dst[2] = pixel[2];
                    dst[3] = pixel[3];
                }
            }
            else
            {
                // Raw packet
                for (CKDWORD i = 0; i < count && pixelCount < totalPixels; i++, pixelCount++)
                {
                    if (srcPos + ctx.srcBytesPerPixel > pixelDataSize)
                        break;
                    CKDWORD fx = pixelCount % ctx.width, fy = pixelCount / ctx.width;
                    CKDWORD dx = MapX(fx, ctx.width, ctx.isRightToLeft);
                    CKDWORD dy = MapY(fy, ctx.height, ctx.isTopDown, ctx.interleaveMode);
                    CKBYTE *dst = dstPixels + dy * dstStride + dx * 4;
                    CKBYTE pixel[4];
                    DecodePixel(&ctx.header, ctx.pixelDepth, ctx.hasColorMap, ctx.isGrayscale,
                                ctx.alphaBits, ctx.colorMap.Begin(), ctx.colorMapEntries,
                                ctx.colorMapBytesPerEntry, srcPixels.Begin() + srcPos, pixel);
                    dst[0] = pixel[0];
                    dst[1] = pixel[1];
                    dst[2] = pixel[2];
                    dst[3] = pixel[3];
                    srcPos += ctx.srcBytesPerPixel;
                }
            }
        }

        if (pixelCount != totalPixels)
        {
            delete[] dstPixels;
            return CKBITMAPERROR_FILECORRUPTED;
        }
    }
    else
    {
        CKDWORD srcStride;
        if (!SafeMul32(ctx.width, ctx.srcBytesPerPixel, srcStride))
        {
            delete[] dstPixels;
            return CKBITMAPERROR_FILECORRUPTED;
        }

        for (CKDWORD fy = 0; fy < ctx.height; fy++)
        {
            CKDWORD dy = MapY(fy, ctx.height, ctx.isTopDown, ctx.interleaveMode);
            CKBYTE *srcRow = srcPixels.Begin() + fy * srcStride;

            for (CKDWORD fx = 0; fx < ctx.width; fx++)
            {
                CKDWORD dx = MapX(fx, ctx.width, ctx.isRightToLeft);
                CKBYTE *dst = dstPixels + dy * dstStride + dx * 4;
                CKBYTE pixel[4];
                DecodePixel(&ctx.header, ctx.pixelDepth, ctx.hasColorMap, ctx.isGrayscale,
                            ctx.alphaBits, ctx.colorMap.Begin(), ctx.colorMapEntries,
                            ctx.colorMapBytesPerEntry, srcRow + fx * ctx.srcBytesPerPixel, pixel);
                dst[0] = pixel[0];
                dst[1] = pixel[1];
                dst[2] = pixel[2];
                dst[3] = pixel[3];
            }
        }
    }

    // Fill properties
    ImageReader::FillFormatBGRA32(props->m_Format, (int)ctx.width, (int)ctx.height, (int)dstStride, dstPixels);
    props->m_Data = dstPixels;

    if (props->m_Size == sizeof(TgaBitmapProperties))
        ((TgaBitmapProperties *)props)->m_BitDepth = OutputHasAlpha(&ctx.header, ctx.pixelDepth,
                                                                    ctx.hasColorMap, ctx.isGrayscale, ctx.alphaBits)
                                                         ? 32
                                                         : 24;

    return 0;
}

//=============================================================================
// TGA_Save - Core Saving Function
//=============================================================================
int TGA_Save(void **outBuffer, CKBitmapProperties *props, int bitDepth, int useRLE)
{
    if (!props || !props->m_Format.Image)
        return 0;

    CKDWORD width = props->m_Format.Width;
    CKDWORD height = props->m_Format.Height;
    CKBYTE *srcPixels = props->m_Format.Image;
    CKDWORD srcStride = props->m_Format.BytesPerLine;

    if (width == 0 || height == 0)
        return 0;
    if (bitDepth != 24 && bitDepth != 32)
        bitDepth = 24;

    CKDWORD dstBpp = bitDepth / 8;

    // Build header
    TGAHEADER header;
    memset(&header, 0, sizeof(header));
    header.imageType = useRLE ? TGA_TYPE_RLE_TRUECOLOR : TGA_TYPE_TRUECOLOR;
    header.width = (CKWORD)width;
    header.height = (CKWORD)height;
    header.pixelDepth = (CKBYTE)bitDepth;
    header.imageDescriptor = (bitDepth == 32) ? 8 : 0;

    // Calculate sizes
    CKDWORD maxPixelDataSize = width * height * dstBpp;
    if (useRLE)
        maxPixelDataSize += width * height / 128 + 1;
    CKDWORD maxFileSize = sizeof(TGAHEADER) + maxPixelDataSize;

    CKBYTE *buffer = new CKBYTE[maxFileSize];
    memcpy(buffer, &header, sizeof(header));
    CKDWORD writePos = sizeof(header);

    if (useRLE)
    {
        CKDWORD totalPixels = width * height;
        CKDWORD pixelIndex = 0;

        while (pixelIndex < totalPixels)
        {
            CKDWORD runStart = pixelIndex;
            CKDWORD runX = runStart % width;
            CKDWORD runY = height - 1 - (runStart / width);
            CKBYTE *startPixel = srcPixels + runY * srcStride + runX * 4;

            // Find RLE run
            CKDWORD rleCount = 1;
            while (rleCount < 128 && (runStart + rleCount) < totalPixels)
            {
                CKDWORD nx = (runStart + rleCount) % width;
                CKDWORD ny = height - 1 - ((runStart + rleCount) / width);
                CKBYTE *nextPixel = srcPixels + ny * srcStride + nx * 4;
                CKBOOL same = TRUE;
                for (CKDWORD i = 0; i < dstBpp && same; i++)
                    if (nextPixel[i] != startPixel[i])
                        same = FALSE;
                if (!same)
                    break;
                rleCount++;
            }

            // Find raw run
            CKDWORD rawCount = 1;
            if (rleCount < 3)
            {
                while (rawCount < 128 && (runStart + rawCount) < totalPixels)
                {
                    CKDWORD cs = runStart + rawCount;
                    CKDWORD cx = cs % width;
                    CKDWORD cy = height - 1 - (cs / width);
                    CKBYTE *cp = srcPixels + cy * srcStride + cx * 4;

                    CKDWORD sameCount = 1;
                    while (sameCount < 3 && (cs + sameCount) < totalPixels)
                    {
                        CKDWORD nxx = (cs + sameCount) % width;
                        CKDWORD nyy = height - 1 - ((cs + sameCount) / width);
                        CKBYTE *np = srcPixels + nyy * srcStride + nxx * 4;
                        CKBOOL same = TRUE;
                        for (CKDWORD i = 0; i < dstBpp && same; i++)
                            if (np[i] != cp[i])
                                same = FALSE;
                        if (!same)
                            break;
                        sameCount++;
                    }
                    if (sameCount >= 3)
                        break;
                    rawCount++;
                }
            }

            if (rleCount >= 3)
            {
                buffer[writePos++] = (CKBYTE)(0x80 | (rleCount - 1));
                for (CKDWORD i = 0; i < dstBpp; i++)
                    buffer[writePos++] = startPixel[i];
                pixelIndex += rleCount;
            }
            else
            {
                buffer[writePos++] = (CKBYTE)(rawCount - 1);
                for (CKDWORD i = 0; i < rawCount; i++)
                {
                    CKDWORD px = (runStart + i) % width;
                    CKDWORD py = height - 1 - ((runStart + i) / width);
                    CKBYTE *p = srcPixels + py * srcStride + px * 4;
                    for (CKDWORD j = 0; j < dstBpp; j++)
                        buffer[writePos++] = p[j];
                }
                pixelIndex += rawCount;
            }
        }
    }
    else
    {
        for (CKDWORD y = 0; y < height; y++)
        {
            CKDWORD srcY = height - 1 - y;
            CKBYTE *srcRow = srcPixels + srcY * srcStride;
            for (CKDWORD x = 0; x < width; x++)
            {
                for (CKDWORD i = 0; i < dstBpp; i++)
                    buffer[writePos++] = srcRow[x * 4 + i];
            }
        }
    }

    CKDWORD fileSize = writePos;

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
        if (fileSize < maxFileSize)
        {
            CKBYTE *final = new CKBYTE[fileSize];
            memcpy(final, buffer, fileSize);
            delete[] buffer;
            buffer = final;
        }
        *outBuffer = buffer;
    }
    return (int)fileSize;
}
