#include "FrameDecoder.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

// stb_image is compiled in StbImageImpl.cpp; we only need the declarations here.
#include "stb_image.h"

static bool MulSizeOverflow(size_t a, size_t b, size_t &out)
{
    if (a != 0 && b > (std::numeric_limits<size_t>::max() / a))
        return true;
    out = a * b;
    return false;
}

static bool ComputeAlignedStride(int width, int bpp, size_t &outStride)
{
    if (width <= 0 || bpp <= 0)
        return false;

    size_t bits = 0;
    if (MulSizeOverflow(static_cast<size_t>(width), static_cast<size_t>(bpp), bits))
        return false;
    if (bits > (std::numeric_limits<size_t>::max() - 31u))
        return false;

    outStride = ((bits + 31u) & ~static_cast<size_t>(31u)) / 8u;
    return true;
}

static bool HasValidOutputStride(int width, int outputStride)
{
    if (width <= 0 || outputStride <= 0)
        return false;
    size_t minStride = 0;
    if (MulSizeOverflow(static_cast<size_t>(width), 4u, minStride))
        return false;
    return static_cast<size_t>(outputStride) >= minStride;
}

static uint8_t ClampByte(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<uint8_t>(v);
}

static uint8_t ExpandBitsTo8(uint32_t v, int bits)
{
    if (bits <= 0) return 0;
    if (bits >= 8) return static_cast<uint8_t>(v >> (bits - 8));
    return static_cast<uint8_t>((v * 255u) / ((1u << bits) - 1u));
}

static uint8_t ChannelFromMask(uint32_t pixel, uint32_t mask)
{
    if (mask == 0)
        return 0;

    int shift = 0;
    while (((mask >> shift) & 1u) == 0u && shift < 32)
        ++shift;

    uint32_t field = (pixel & mask) >> shift;
    int bits = 0;
    uint32_t m = mask >> shift;
    while ((m & 1u) != 0u)
    {
        ++bits;
        m >>= 1;
    }

    return ExpandBitsTo8(field, bits);
}

static void YuvToBgr(uint8_t y, uint8_t u, uint8_t v, uint8_t &outB, uint8_t &outG, uint8_t &outR)
{
    const int c = static_cast<int>(y) - 16;
    const int d = static_cast<int>(u) - 128;
    const int e = static_cast<int>(v) - 128;

    const int r = (298 * c + 409 * e + 128) >> 8;
    const int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    const int b = (298 * c + 516 * d + 128) >> 8;

    outR = ClampByte(r);
    outG = ClampByte(g);
    outB = ClampByte(b);
}

/// Map source row index to account for top-down vs bottom-up source storage.
/// Output is always bottom-up, so for bottom-up source row y stays y.
static int SourceRow(bool srcTopDown, int y, int height)
{
    return srcTopDown ? (height - 1 - y) : y;
}

/// Write a 32bpp BGRA pixel at dst[x*4..x*4+3].
static void WriteBgra(uint8_t *dst, int x, uint8_t b, uint8_t g, uint8_t r, uint8_t a = 0xFF)
{
    dst[x * 4 + 0] = b;
    dst[x * 4 + 1] = g;
    dst[x * 4 + 2] = r;
    dst[x * 4 + 3] = a;
}

/// Write a packed ARGB word at dst[0..3] in BGRA byte order.
static void WriteArgb(uint8_t *dst, uint32_t argb)
{
    dst[0] = static_cast<uint8_t>(argb        & 0xFFu);
    dst[1] = static_cast<uint8_t>((argb >> 8)  & 0xFFu);
    dst[2] = static_cast<uint8_t>((argb >> 16) & 0xFFu);
    dst[3] = static_cast<uint8_t>((argb >> 24) & 0xFFu);
}

/// Resolve a palette index to a packed 32bpp ARGB colour.
/// Falls back to a greyscale value when the palette is empty or the index is out of range.
static void InitializeFramePalette(uint32_t *colors, const avi::AviStreamInfo &info)
{
    for (unsigned i = 0; i < 256; ++i)
        colors[i] = i < info.palette.size() ? info.palette[i] : (0xff000000u | i * 0x010101u);
}

static bool UpdateFramePalette(uint32_t *current, const uint32_t *colors)
{
    if (memcmp(current, colors, 256 * sizeof(uint32_t)) == 0)
        return false;
    memcpy(current, colors, 256 * sizeof(uint32_t));
    return true;
}

static uint32_t ResolvePaletteEntry(const uint32_t *palette, uint8_t index)
{
    return palette[index];
}

static uint8_t ReadIndexedPixel(const uint8_t *src, int x, int bpp)
{
    if (bpp == 8)
        return src[x];
    if (bpp == 4)
        return static_cast<uint8_t>((x & 1) ? (src[x >> 1] & 0x0Fu) : (src[x >> 1] >> 4));
    if (bpp == 1)
        return static_cast<uint8_t>((src[x >> 3] >> (7 - (x & 7))) & 1u);
    return 0;
}

static uint8_t HighNibble(uint8_t value)
{
    return static_cast<uint8_t>(value >> 4);
}

static uint8_t LowNibble(uint8_t value)
{
    return static_cast<uint8_t>(value & 0x0Fu);
}

static bool ValidArgs(const uint8_t *data, const uint8_t *output, int width, int height, int outputStride)
{
    return data && output && width > 0 && height > 0 && outputStride > 0;
}

// ===========================================================================
// Factory
// ===========================================================================

std::unique_ptr<IFrameDecoder> CreateFrameDecoder(const avi::AviStreamInfo &info)
{
    if (info.codec == avi::kCodec_RGB || info.codec == avi::kCodec_BITFIELDS)
    {
        if (info.bitsPerPixel == 1 || info.bitsPerPixel == 4 ||
            info.bitsPerPixel == 8 || info.bitsPerPixel == 16 ||
            info.bitsPerPixel == 24 || info.bitsPerPixel == 32)
            return std::make_unique<RawFrameDecoder>(info);
        return nullptr;
    }

    if (info.codec == avi::kCodec_RLE8)
    {
        if (info.bitsPerPixel == 8)
            return std::make_unique<Rle8FrameDecoder>(info);
        return nullptr;
    }

    if (info.codec == avi::kCodec_RLE4)
    {
        if (info.bitsPerPixel == 4)
            return std::make_unique<Rle4FrameDecoder>(info);
        return nullptr;
    }

    if (info.codec == avi::kCodec_MJPG || info.codec == avi::kCodec_mjpg)
    {
        return std::make_unique<MjpegFrameDecoder>();
    }

    if (info.codec == avi::kCodec_CRAM || info.codec == avi::kCodec_MSVC || info.codec == avi::kCodec_WHAM || info.codec == avi::MakeFourCC('M', 'S', 'V', 'C'))
    {
        // Microsoft Video 1 has both indexed 8bpp and RGB555 16bpp variants.
        if (info.bitsPerPixel == 8 || info.bitsPerPixel == 16)
            return std::make_unique<Msvideo1FrameDecoder>(info);
        return nullptr;
    }

    if (info.codec == avi::kCodec_YUY2)
        return std::make_unique<PackedYuv422FrameDecoder>(info.videoTopDown, false);
    if (info.codec == avi::kCodec_UYVY)
        return std::make_unique<PackedYuv422FrameDecoder>(info.videoTopDown, true);

    return nullptr; // unsupported codec
}

// ===========================================================================
// RawFrameDecoder  (BI_RGB / BI_BITFIELDS, 1/4/8/16/24/32 bpp)
// ===========================================================================

RawFrameDecoder::RawFrameDecoder(const avi::AviStreamInfo &info)
    : m_SrcBpp(info.bitsPerPixel),
      m_SrcTopDown(info.videoTopDown),
      m_RedMask(info.redMask),
      m_GreenMask(info.greenMask),
      m_BlueMask(info.blueMask),
      m_AlphaMask(info.alphaMask)
{
    InitializeFramePalette(m_Palette, info);
    if (m_SrcBpp == 16 && (m_RedMask | m_GreenMask | m_BlueMask) == 0)
    {
        m_RedMask = 0x00007C00u;
        m_GreenMask = 0x000003E0u;
        m_BlueMask = 0x0000001Fu;
    }
    if (m_SrcBpp == 32 && (m_RedMask | m_GreenMask | m_BlueMask) == 0)
    {
        m_RedMask = 0x00FF0000u;
        m_GreenMask = 0x0000FF00u;
        m_BlueMask = 0x000000FFu;
    }
}

bool RawFrameDecoder::SetPalette(const uint32_t *colors)
{
    return UpdateFramePalette(m_Palette, colors);
}

bool RawFrameDecoder::Decode(const uint8_t *data, size_t dataSize,
                             int width, int height,
                             uint8_t *output, int outputStride)
{
    if (!ValidArgs(data, output, width, height, outputStride))
        return false;
    if (!HasValidOutputStride(width, outputStride))
        return false;

    size_t srcStride = 0;
    if (!ComputeAlignedStride(width, m_SrcBpp, srcStride))
        return false;
    size_t neededSrc = 0;
    if (MulSizeOverflow(srcStride, static_cast<size_t>(height), neededSrc))
        return false;
    if (dataSize < neededSrc)
        return false;

    for (int y = 0; y < height; ++y)
    {
        const int srcY = SourceRow(m_SrcTopDown, y, height);
        const uint8_t *src = data + static_cast<size_t>(srcY) * srcStride;
        uint8_t *dst = output + static_cast<size_t>(y) * static_cast<size_t>(outputStride);

        if (m_SrcBpp == 32)
        {
            for (int x = 0; x < width; ++x)
            {
                const uint32_t px =
                    static_cast<uint32_t>(src[x * 4 + 0]) |
                    (static_cast<uint32_t>(src[x * 4 + 1]) << 8) |
                    (static_cast<uint32_t>(src[x * 4 + 2]) << 16) |
                    (static_cast<uint32_t>(src[x * 4 + 3]) << 24);

                const uint8_t r = ChannelFromMask(px, m_RedMask);
                const uint8_t g = ChannelFromMask(px, m_GreenMask);
                const uint8_t b = ChannelFromMask(px, m_BlueMask);
                const uint8_t a = (m_AlphaMask != 0) ? ChannelFromMask(px, m_AlphaMask) : 0xFF;

                WriteBgra(dst, x, b, g, r, a);
            }
        }
        else if (m_SrcBpp == 24)
        {
            for (int x = 0; x < width; ++x)
                WriteBgra(dst, x, src[x * 3 + 0], src[x * 3 + 1], src[x * 3 + 2]);
        }
        else if (m_SrcBpp == 16)
        {
            for (int x = 0; x < width; ++x)
            {
                const uint16_t px = avi::ReadLe16(&src[x * 2]);
                const uint8_t r = ChannelFromMask(px, m_RedMask);
                const uint8_t g = ChannelFromMask(px, m_GreenMask);
                const uint8_t b = ChannelFromMask(px, m_BlueMask);
                const uint8_t a = (m_AlphaMask != 0) ? ChannelFromMask(px, m_AlphaMask) : 0xFF;

                WriteBgra(dst, x, b, g, r, a);
            }
        }
        else if (m_SrcBpp == 8 || m_SrcBpp == 4 || m_SrcBpp == 1)
        {
            for (int x = 0; x < width; ++x)
                WriteArgb(&dst[x * 4], ResolvePaletteEntry(m_Palette, ReadIndexedPixel(src, x, m_SrcBpp)));
        }
        else
        {
            return false;
        }
    }

    return true;
}

// ===========================================================================
// Rle8FrameDecoder  (BI_RLE8)
// ===========================================================================

Rle8FrameDecoder::Rle8FrameDecoder(const avi::AviStreamInfo &info)
    : m_SrcTopDown(info.videoTopDown)
{
    InitializeFramePalette(m_Palette, info);
}

void DeltaFrameDecoder::Reset()
{
    m_PreviousFrame.clear();
    m_Width = 0;
    m_Height = 0;
}

size_t DeltaFrameDecoder::BeginFrame(uint8_t *output, int width, int height, int outputStride)
{
    size_t frameBytes = 0;
    if (MulSizeOverflow(static_cast<size_t>(outputStride), static_cast<size_t>(height), frameBytes))
        return 0;
    if (m_Width != width || m_Height != height || m_PreviousFrame.size() != frameBytes)
    {
        m_Width = width;
        m_Height = height;
        try { m_PreviousFrame.assign(frameBytes, 0); }
        catch (...) { return 0; }
    }
    memcpy(output, m_PreviousFrame.data(), frameBytes);
    return frameBytes;
}

void DeltaFrameDecoder::CommitFrame(const uint8_t *output, size_t frameBytes)
{
    memcpy(m_PreviousFrame.data(), output, frameBytes);
}

bool Rle8FrameDecoder::SetPalette(const uint32_t *colors)
{
    return UpdateFramePalette(m_Palette, colors);
}

bool Rle8FrameDecoder::Decode(const uint8_t *data, size_t dataSize,
                              int width, int height,
                              uint8_t *output, int outputStride)
{
    if (!ValidArgs(data, output, width, height, outputStride))
        return false;
    if (!HasValidOutputStride(width, outputStride))
        return false;

    const bool firstFrame = m_Width != width || m_Height != height || m_PreviousFrame.empty();
    const size_t frameBytes = BeginFrame(output, width, height, outputStride);
    if (frameBytes == 0)
        return false;

    auto putPixel = [&](int x, int y, uint8_t index) -> bool
    {
        if (x < 0 || x >= width || y < 0 || y >= height)
            return false;
        uint8_t *dst = output + static_cast<size_t>(y) * static_cast<size_t>(outputStride) + static_cast<size_t>(x) * 4;

        WriteArgb(dst, ResolvePaletteEntry(m_Palette, index));
        return true;
    };

    if (firstFrame)
        for (int row = 0; row < height; ++row)
            for (int column = 0; column < width; ++column)
                WriteArgb(output + size_t(row) * outputStride + size_t(column) * 4, m_Palette[0]);

    int x = 0;
    int y = m_SrcTopDown ? (height - 1) : 0;
    const int yStep = m_SrcTopDown ? -1 : 1;

    const uint8_t *p = data;
    const uint8_t *end = data + dataSize;

    while (p < end)
    {
        if (end - p < 2)
            return false;

        const uint8_t count = *p++;
        const uint8_t value = *p++;

        if (count > 0)
        {
            for (int i = 0; i < count; ++i)
            {
                if (!putPixel(x, y, value))
                    return false;
                ++x;
            }
            continue;
        }

        if (value == 0)
        {
            // End of line.
            x = 0;
            y += yStep;
            if (y < 0 || y >= height)
                break;
        }
        else if (value == 1)
        {
            // End of bitmap.
            break;
        }
        else if (value == 2)
        {
            // Delta.
            if (end - p < 2)
                return false;
            const uint8_t dx = *p++;
            const uint8_t dy = *p++;
            x += dx;
            y += yStep * dy;
            if (x < 0 || x >= width || y < 0 || y >= height)
                return false;
        }
        else
        {
            // Absolute run.
            const int n = value;
            if (end - p < n)
                return false;
            for (int i = 0; i < n; ++i)
            {
                if (!putPixel(x, y, p[i]))
                    return false;
                ++x;
            }
            p += n;
            if (n & 1)
            {
                if (p >= end)
                    return false;
                ++p; // pad byte
            }
        }
    }

    CommitFrame(output, frameBytes);
    return true;
}

// ===========================================================================
// Rle4FrameDecoder  (BI_RLE4)
// ===========================================================================

Rle4FrameDecoder::Rle4FrameDecoder(const avi::AviStreamInfo &info)
    : m_SrcTopDown(info.videoTopDown)
{
    InitializeFramePalette(m_Palette, info);
}

bool Rle4FrameDecoder::SetPalette(const uint32_t *colors)
{
    return UpdateFramePalette(m_Palette, colors);
}

bool Rle4FrameDecoder::Decode(const uint8_t *data, size_t dataSize,
                              int width, int height,
                              uint8_t *output, int outputStride)
{
    if (!ValidArgs(data, output, width, height, outputStride))
        return false;
    if (!HasValidOutputStride(width, outputStride))
        return false;

    const bool firstFrame = m_Width != width || m_Height != height || m_PreviousFrame.empty();
    const size_t frameBytes = BeginFrame(output, width, height, outputStride);
    if (frameBytes == 0)
        return false;

    auto putPixel = [&](int x, int y, uint8_t index) -> bool
    {
        if (x < 0 || x >= width || y < 0 || y >= height)
            return false;
        uint8_t *dst = output + static_cast<size_t>(y) * static_cast<size_t>(outputStride) + static_cast<size_t>(x) * 4;

        WriteArgb(dst, ResolvePaletteEntry(m_Palette, index));
        return true;
    };

    if (firstFrame)
        for (int row = 0; row < height; ++row)
            for (int column = 0; column < width; ++column)
                WriteArgb(output + size_t(row) * outputStride + size_t(column) * 4, m_Palette[0]);

    int x = 0;
    int y = m_SrcTopDown ? (height - 1) : 0;
    const int yStep = m_SrcTopDown ? -1 : 1;

    const uint8_t *p = data;
    const uint8_t *end = data + dataSize;

    while (p < end)
    {
        if (end - p < 2)
            return false;

        const uint8_t count = *p++;
        const uint8_t value = *p++;

        // Runs that overhang the row are clipped like the BMP RLE4 decoder
        // does; encoders often round odd-width runs up to whole bytes.
        if (count > 0)
        {
            for (int i = 0; i < count && x < width; ++i)
            {
                const uint8_t index = (i & 1) ? LowNibble(value) : HighNibble(value);
                if (!putPixel(x, y, index))
                    return false;
                ++x;
            }
            continue;
        }

        if (value == 0)
        {
            x = 0;
            y += yStep;
            if (y < 0 || y >= height)
                break;
        }
        else if (value == 1)
        {
            break;
        }
        else if (value == 2)
        {
            if (end - p < 2)
                return false;
            const uint8_t dx = *p++;
            const uint8_t dy = *p++;
            x += dx;
            y += yStep * dy;
            if (x < 0 || x >= width || y < 0 || y >= height)
                return false;
        }
        else
        {
            const int n = value;
            const int dataBytes = (n + 1) / 2;
            const int paddedBytes = (dataBytes + 1) & ~1;
            if (end - p < paddedBytes)
                return false;
            for (int i = 0; i < n && x < width; ++i)
            {
                const uint8_t packed = p[i >> 1];
                const uint8_t index = (i & 1) ? LowNibble(packed) : HighNibble(packed);
                if (!putPixel(x, y, index))
                    return false;
                ++x;
            }
            p += paddedBytes;
        }
    }

    CommitFrame(output, frameBytes);
    return true;
}

// ===========================================================================
// PackedYuv422FrameDecoder  (YUY2 / UYVY)
// ===========================================================================

PackedYuv422FrameDecoder::PackedYuv422FrameDecoder(bool srcTopDown, bool uyvyLayout)
    : m_SrcTopDown(srcTopDown), m_Uyvy(uyvyLayout)
{}

bool PackedYuv422FrameDecoder::Decode(const uint8_t *data, size_t dataSize,
                                      int width, int height,
                                      uint8_t *output, int outputStride)
{
    if (!ValidArgs(data, output, width, height, outputStride))
        return false;
    if (!HasValidOutputStride(width, outputStride))
        return false;
    if (width & 1)
        return false;

    size_t srcStride = 0;
    if (MulSizeOverflow(static_cast<size_t>(width), 2u, srcStride))
        return false;
    size_t needed = 0;
    if (MulSizeOverflow(srcStride, static_cast<size_t>(height), needed))
        return false;
    if (dataSize < needed)
        return false;

    for (int y = 0; y < height; ++y)
    {
        const int srcY = SourceRow(m_SrcTopDown, y, height);
        const uint8_t *src = data + static_cast<size_t>(srcY) * srcStride;
        uint8_t *dst = output + static_cast<size_t>(y) * static_cast<size_t>(outputStride);

        for (int x = 0; x < width; x += 2)
        {
            uint8_t y0, y1, u, v;
            if (!m_Uyvy)
            {
                // YUY2: Y0 U Y1 V
                y0 = src[x * 2 + 0];
                u  = src[x * 2 + 1];
                y1 = src[x * 2 + 2];
                v  = src[x * 2 + 3];
            }
            else
            {
                // UYVY: U Y0 V Y1
                u  = src[x * 2 + 0];
                y0 = src[x * 2 + 1];
                v  = src[x * 2 + 2];
                y1 = src[x * 2 + 3];
            }

            uint8_t b0, g0, r0;
            uint8_t b1, g1, r1;
            YuvToBgr(y0, u, v, b0, g0, r0);
            YuvToBgr(y1, u, v, b1, g1, r1);

            WriteBgra(dst, x,     b0, g0, r0);
            WriteBgra(dst, x + 1, b1, g1, r1);
        }
    }

    return true;
}

// ===========================================================================
// MjpegFrameDecoder  (MJPEG via stb_image)
// ===========================================================================

// ISO/IEC 10918-1 AnnexK conventional DC/AC tables0/1, in JPEG DHT payload order.
static const uint8_t kMjpegDefaultHuffmanTables[] = {
    0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x10, 0x00, 0x02,
    0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7d, 0x01, 0x02,
    0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71,
    0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33,
    0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a,
    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53,
    0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73,
    0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92,
    0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9,
    0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4,
    0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
    0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x11, 0x00, 0x02,
    0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01,
    0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62,
    0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
    0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a,
    0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3,
    0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
};

// Supply only baseline tables that the first scan actually needs and did not
// define. An explicit table (including a custom table) always takes precedence.
static int PrepareMjpegHuffman(const uint8_t *data, size_t size, uint8_t *prefix, size_t &insertAt)
{
    if (size < 2 || data[0] != 255 || data[1] != 0xd8)
        return -1;
    unsigned defined[2] = {};
    bool baseline = false;
    size_t p = 2;
    while (p < size)
    {
        const size_t markerStart = p;
        if (data[p++] != 255)
            return -1;
        while (p < size && data[p] == 255)
            ++p;
        if (p == size)
            return -1;
        const unsigned marker = data[p++];
        if (p + 2 > size)
            return -1;
        const unsigned length = (unsigned(data[p]) << 8) | data[p + 1];
        if (length < 2 || length > size - p)
            return -1;
        const size_t end = p + length;
        if (marker == 0xc0)
            baseline = true;
        if (marker == 0xc4)
        {
            size_t table = p + 2;
            while (table < end)
            {
                if (end - table < 17)
                    return -1;
                const unsigned selector = data[table++];
                if ((selector >> 4) > 1 || (selector & 15) > 3)
                    return -1;
                unsigned symbols = 0;
                for (unsigned i = 0; i < 16; ++i)
                    symbols += data[table++];
                if (symbols == 0 || symbols > 256 || symbols > end - table)
                    return -1;
                defined[selector >> 4] |= 1u << (selector & 15);
                table += symbols;
            }
        }
        if (marker == 0xda)
        {
            // Progressive JPEG can define AC tables only in a later scan.
            if (!baseline)
                return 0;
            if (length < 6)
                return -1;
            const unsigned components = data[p + 2];
            if (components == 0 || components > 4 || length != 6 + 2 * components)
                return -1;
            unsigned missing[2] = {};
            for (unsigned i = 0; i < components; ++i)
            {
                const unsigned selectors = data[p + 4 + i * 2];
                for (unsigned kind = 0; kind < 2; ++kind)
                {
                    const unsigned id = kind ? selectors & 15 : selectors >> 4;
                    if (id > 3)
                        return -1;
                    if (!(defined[kind] & (1u << id)))
                    {
                        if (id > 1)
                            return -1;
                        missing[kind] |= 1u << id;
                    }
                }
            }
            if (!(missing[0] | missing[1]))
                return 0;
            unsigned used = 4;
            size_t table = 0;
            while (table < sizeof(kMjpegDefaultHuffmanTables))
            {
                const unsigned selector = kMjpegDefaultHuffmanTables[table];
                unsigned n = 17;
                for (unsigned i = 1; i <= 16; ++i)
                    n += kMjpegDefaultHuffmanTables[table + i];
                if (missing[selector >> 4] & (1u << (selector & 15)))
                {
                    memcpy(prefix + used, kMjpegDefaultHuffmanTables + table, n);
                    used += n;
                }
                table += n;
            }
            prefix[0] = 255;
            prefix[1] = 0xc4;
            prefix[2] = uint8_t((used - 2) >> 8);
            prefix[3] = uint8_t(used - 2);
            insertAt = markerStart;
            return int(used);
        }
        p = end;
    }
    return -1;
}

bool MjpegFrameDecoder::Decode(const uint8_t *data, size_t dataSize,
                               int width, int height,
                               uint8_t *output, int outputStride)
{
    if (!ValidArgs(data, output, width, height, outputStride))
        return false;
    if (!HasValidOutputStride(width, outputStride))
        return false;
    if (dataSize > static_cast<size_t>(std::numeric_limits<int>::max()))
        return false;

    uint8_t prefix[420];
    size_t insertAt = 0;
    const int extra = PrepareMjpegHuffman(data, dataSize, prefix, insertAt);
    if (extra < 0)
        return false;
    uint8_t *expanded = nullptr;
    if (extra)
    {
        if (dataSize > size_t(std::numeric_limits<int>::max() - extra))
            return false;
        expanded = new (std::nothrow) uint8_t[dataSize + extra];
        if (!expanded)
            return false;
        memcpy(expanded, data, insertAt);
        memcpy(expanded + insertAt, prefix, extra);
        memcpy(expanded + insertAt + extra, data + insertAt, dataSize - insertAt);
        data = expanded;
        dataSize += extra;
    }

    // stb_image decodes JPEG data to top-down RGB (3 channels).
    int imgW = 0, imgH = 0, imgComp = 0;
    unsigned char *pixels = stbi_load_from_memory(
        data, static_cast<int>(dataSize), &imgW, &imgH, &imgComp, 3);
    delete[] expanded;

    if (!pixels)
        return false;

    // Dimension mismatch is an error -- the frame must match the header.
    if (imgW != width || imgH != height)
    {
        stbi_image_free(pixels);
        return false;
    }

    // Convert top-down RGB -> bottom-up 32bpp ARGB.
    size_t srcStride = 0;
    if (MulSizeOverflow(static_cast<size_t>(imgW), 3u, srcStride))
    {
        stbi_image_free(pixels);
        return false;
    }
    for (int y = 0; y < height; ++y)
    {
        const int srcY = SourceRow(true, y, height);
        const uint8_t *src = pixels + static_cast<size_t>(srcY) * srcStride;
        uint8_t *dst = output + static_cast<size_t>(y) * static_cast<size_t>(outputStride);

        for (int x = 0; x < width; ++x)
                WriteBgra(dst, x, src[x * 3 + 2], src[x * 3 + 1], src[x * 3 + 0]);
    }

    stbi_image_free(pixels);
    return true;
}

// ===========================================================================
// Msvideo1FrameDecoder  (Microsoft Video 1 / CRAM, indexed8 and RGB555)
// ===========================================================================

/// 5-bit -> 8-bit expansion with bit replication.
static uint8_t Expand5To8(uint32_t v)
{
    return static_cast<uint8_t>((v << 3) | (v >> 2));
}

/// Unpack a 15-bit RGB555 colour word to 32bpp ARGB.
static uint32_t Rgb555ToArgb32(uint16_t c)
{
    c &= 0x7FFFu;
    // Microsoft Video 1 16bpp uses RGB555 (R: bits 10-14, G: 5-9, B: 0-4).
    const uint32_t r = Expand5To8((c >> 10) & 0x1Fu);
    const uint32_t g = Expand5To8((c >> 5)  & 0x1Fu);
    const uint32_t b = Expand5To8(c         & 0x1Fu);
    // ARGB word; in little-endian memory this is B, G, R, A.
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

Msvideo1FrameDecoder::Msvideo1FrameDecoder(const avi::AviStreamInfo &info)
    : m_SrcBpp(info.bitsPerPixel)
{
    InitializeFramePalette(m_Palette, info);
}

bool Msvideo1FrameDecoder::SetPalette(const uint32_t *colors)
{
    return UpdateFramePalette(m_Palette, colors);
}

bool Msvideo1FrameDecoder::Decode(const uint8_t *data, size_t dataSize,
                                  int width, int height,
                                  uint8_t *output, int outputStride)
{
    if (!ValidArgs(data, output, width, height, outputStride))
        return false;
    if ((width % 4) != 0 || (height % 4) != 0)
        return false;
    if ((outputStride % 4) != 0 || outputStride < width * 4)
        return false;

    const size_t frameBytes = BeginFrame(output, width, height, outputStride);
    if (frameBytes == 0)
        return false;

    // Delta/skipped blocks refer to previous frame contents.

    const uint8_t *p = data;
    const uint8_t *end = data + dataSize;

    const int blocksWide = width / 4;
    const int blocksHigh = height / 4;
    int totalBlocks = blocksWide * blocksHigh;
    int skipBlocks = 0;

    uint32_t *dst = reinterpret_cast<uint32_t *>(output);
    const int stridePixels = outputStride / 4;

    for (int blockY = blocksHigh; blockY > 0; --blockY)
    {
        // Bitstream macroblock order matches bottom-up DIB order. Convert that
        // order into our bottom-up destination layout directly.
        const int baseRow = (blocksHigh - blockY) * 4;
        uint32_t *blockPtr = dst + baseRow * stridePixels;

        for (int blockX = blocksWide; blockX > 0; --blockX)
        {
            --totalBlocks;

            if (skipBlocks > 0)
            {
                --skipBlocks;
                blockPtr += 4;
                continue;
            }

            if (end - p < 2)
                return false;

            const uint8_t byteA = *p++;
            const uint8_t byteB = *p++;

            // Skip-code: keep current block from previous frame and skip N-1 next blocks.
            if ((byteB & 0xFCu) == 0x84u)
            {
                const int count = ((static_cast<int>(byteB) - 0x84) << 8) + static_cast<int>(byteA);
                if (count <= 0)
                    return false;
                skipBlocks = count - 1;
                if (skipBlocks > totalBlocks)
                    return false;

                blockPtr += 4;
                continue;
            }

            if (m_SrcBpp == 8)
            {
                uint16_t flags = uint16_t(byteA) | (uint16_t(byteB) << 8);
                const int colorCount = byteB < 0x80 ? 2 : byteB >= 0x90 ? 8 : 1;
                uint32_t colors[8];
                if (colorCount == 1)
                    colors[0] = m_Palette[byteA];
                else
                {
                    if (end - p < colorCount)
                        return false;
                    for (int i = 0; i < colorCount; ++i)
                        colors[i] = m_Palette[*p++];
                }
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x)
                    {
                        int index = 0;
                        if (colorCount > 1)
                        {
                            index = (flags & 1u) ^ 1u;
                            if (colorCount == 8)
                                index += ((y & 2) << 1) + (x & 2);
                            flags >>= 1;
                        }
                        blockPtr[y * stridePixels + x] = colors[index];
                    }
                blockPtr += 4;
                continue;
            }

            if (byteB < 0x80u)
            {
                // Two-color / eight-color block with 16-bit flags.
                if (end - p < 4)
                    return false;

                uint16_t flags = static_cast<uint16_t>(byteA) |
                                 (static_cast<uint16_t>(byteB) << 8);
                uint16_t c0 = avi::ReadLe16(p); p += 2;
                uint16_t c1 = avi::ReadLe16(p); p += 2;

                if ((c0 & 0x8000u) != 0)
                {
                    // 8-color mode: select one of 4 colors per 2x2 quadrant.
                    if (end - p < 12)
                        return false;

                    uint32_t colors[8];
                    colors[0] = Rgb555ToArgb32(c0);
                    colors[1] = Rgb555ToArgb32(c1);
                    for (int i = 2; i < 8; ++i)
                    {
                        colors[i] = Rgb555ToArgb32(avi::ReadLe16(p));
                        p += 2;
                    }

                    for (int py = 0; py < 4; ++py)
                    {
                        uint32_t *row = blockPtr + py * stridePixels;
                        for (int px = 0; px < 4; ++px)
                        {
                            const int colorBase = ((py & 2) << 1) + (px & 2);
                            const int idx = colorBase + ((flags & 1u) ^ 1u);
                            row[px] = colors[idx];
                            flags >>= 1;
                        }
                    }
                }
                else
                {
                    // 2-color mode.
                    const uint32_t colors[2] = {
                        Rgb555ToArgb32(c0),
                        Rgb555ToArgb32(c1)
                    };

                    for (int py = 0; py < 4; ++py)
                    {
                        uint32_t *row = blockPtr + py * stridePixels;
                        for (int px = 0; px < 4; ++px)
                        {
                            row[px] = colors[(flags & 1u) ^ 1u];
                            flags >>= 1;
                        }
                    }
                }
            }
            else
            {
                // One-color block (single 15-bit color repeated over 4x4).
                const uint32_t color = Rgb555ToArgb32(
                    static_cast<uint16_t>(byteA | (static_cast<uint16_t>(byteB) << 8)));

                for (int py = 0; py < 4; ++py)
                {
                    uint32_t *row = blockPtr + py * stridePixels;
                    row[0] = color;
                    row[1] = color;
                    row[2] = color;
                    row[3] = color;
                }
            }

            blockPtr += 4;
        }
    }

    CommitFrame(output, frameBytes);
    return true;
}
