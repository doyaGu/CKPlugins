#include "FrameDecoder.h"

#include <algorithm>
#include <cstring>
#include <limits>

// stb_image is compiled in StbImageImpl.cpp; we only need the declarations here.
#include "stb_image.h"

namespace
{

bool MulSizeOverflow(size_t a, size_t b, size_t &out)
{
    if (a != 0 && b > (std::numeric_limits<size_t>::max() / a))
        return true;
    out = a * b;
    return false;
}

bool ComputeAlignedStride(int width, int bpp, size_t &outStride)
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

bool HasValidOutputStride(int width, int outputStride)
{
    if (width <= 0 || outputStride <= 0)
        return false;
    size_t minStride = 0;
    if (MulSizeOverflow(static_cast<size_t>(width), 4u, minStride))
        return false;
    return static_cast<size_t>(outputStride) >= minStride;
}

uint8_t ClampByte(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return static_cast<uint8_t>(v);
}

uint8_t ExpandBitsTo8(uint32_t v, int bits)
{
    if (bits <= 0) return 0;
    if (bits >= 8) return static_cast<uint8_t>(v >> (bits - 8));
    return static_cast<uint8_t>((v * 255u) / ((1u << bits) - 1u));
}

uint8_t ChannelFromMask(uint32_t pixel, uint32_t mask)
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

void YuvToBgr(uint8_t y, uint8_t u, uint8_t v, uint8_t &outB, uint8_t &outG, uint8_t &outR)
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
int SourceRow(bool srcTopDown, int y, int height)
{
    return srcTopDown ? (height - 1 - y) : y;
}

/// Write a 32bpp BGRA pixel at dst[x*4..x*4+3].
void WriteBgra(uint8_t *dst, int x, uint8_t b, uint8_t g, uint8_t r, uint8_t a = 0xFF)
{
    dst[x * 4 + 0] = b;
    dst[x * 4 + 1] = g;
    dst[x * 4 + 2] = r;
    dst[x * 4 + 3] = a;
}

/// Write a packed ARGB word at dst[0..3] in BGRA byte order.
void WriteArgb(uint8_t *dst, uint32_t argb)
{
    dst[0] = static_cast<uint8_t>(argb        & 0xFFu);
    dst[1] = static_cast<uint8_t>((argb >> 8)  & 0xFFu);
    dst[2] = static_cast<uint8_t>((argb >> 16) & 0xFFu);
    dst[3] = static_cast<uint8_t>((argb >> 24) & 0xFFu);
}

/// Resolve a palette index to a packed 32bpp ARGB colour.
/// Falls back to a greyscale value when the palette is empty or the index is out of range.
uint32_t ResolvePaletteEntry(const std::vector<uint32_t> &palette, uint8_t index)
{
    if (!palette.empty() && index < palette.size())
        return palette[index];
    return 0xFF000000u |
           (static_cast<uint32_t>(index) << 16) |
           (static_cast<uint32_t>(index) << 8)  |
           index;
}

uint8_t ReadIndexedPixel(const uint8_t *src, int x, int bpp)
{
    if (bpp == 8)
        return src[x];
    if (bpp == 4)
        return static_cast<uint8_t>((x & 1) ? (src[x >> 1] & 0x0Fu) : (src[x >> 1] >> 4));
    if (bpp == 1)
        return static_cast<uint8_t>((src[x >> 3] >> (7 - (x & 7))) & 1u);
    return 0;
}

uint8_t HighNibble(uint8_t value)
{
    return static_cast<uint8_t>(value >> 4);
}

uint8_t LowNibble(uint8_t value)
{
    return static_cast<uint8_t>(value & 0x0Fu);
}

bool ValidArgs(const uint8_t *data, const uint8_t *output, int width, int height, int outputStride)
{
    return data && output && width > 0 && height > 0 && outputStride > 0;
}

} // namespace

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

    if (info.codec == avi::kCodec_CRAM || info.codec == avi::kCodec_MSVC || info.codec == avi::kCodec_WHAM)
    {
        // We currently support the common 16bpp CRAM variant.
        if (info.bitsPerPixel == 16)
            return std::make_unique<Msvideo1FrameDecoder>();
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
      m_AlphaMask(info.alphaMask),
      m_Palette(info.palette)
{
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
    : m_SrcTopDown(info.videoTopDown),
      m_Palette(info.palette)
{}

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

bool Rle8FrameDecoder::Decode(const uint8_t *data, size_t dataSize,
                              int width, int height,
                              uint8_t *output, int outputStride)
{
    if (!ValidArgs(data, output, width, height, outputStride))
        return false;
    if (!HasValidOutputStride(width, outputStride))
        return false;

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
    : m_SrcTopDown(info.videoTopDown),
      m_Palette(info.palette)
{}

bool Rle4FrameDecoder::Decode(const uint8_t *data, size_t dataSize,
                              int width, int height,
                              uint8_t *output, int outputStride)
{
    if (!ValidArgs(data, output, width, height, outputStride))
        return false;
    if (!HasValidOutputStride(width, outputStride))
        return false;

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
            for (int i = 0; i < n; ++i)
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

    // stb_image decodes JPEG data to top-down RGB (3 channels).
    int imgW = 0, imgH = 0, imgComp = 0;
    unsigned char *pixels = stbi_load_from_memory(
        data, static_cast<int>(dataSize), &imgW, &imgH, &imgComp, 3);

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
// Msvideo1FrameDecoder  (Microsoft Video 1 / CRAM, 16bpp)
// ===========================================================================

namespace
{

/// 5-bit -> 8-bit expansion with bit replication.
uint8_t Expand5To8(uint32_t v)
{
    return static_cast<uint8_t>((v << 3) | (v >> 2));
}

/// Unpack a 15-bit RGB555 colour word to 32bpp ARGB.
uint32_t Rgb555ToArgb32(uint16_t c)
{
    c &= 0x7FFFu;
    // Microsoft Video 1 16bpp uses RGB555 (R: bits 10-14, G: 5-9, B: 0-4).
    const uint32_t r = Expand5To8((c >> 10) & 0x1Fu);
    const uint32_t g = Expand5To8((c >> 5)  & 0x1Fu);
    const uint32_t b = Expand5To8(c         & 0x1Fu);
    // ARGB word; in little-endian memory this is B, G, R, A.
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

} // namespace

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

            // End marker used by some encoders as trailing pad.
            if (byteA == 0 && byteB == 0 && totalBlocks == 0)
            {
                CommitFrame(output, frameBytes);
                return true;
            }

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
