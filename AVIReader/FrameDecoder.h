#ifndef FRAMEDECODER_H
#define FRAMEDECODER_H

#include "AviTypes.h"

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// IFrameDecoder -- abstract interface for decoding a single compressed
// (or uncompressed) video frame payload into a 32bpp ARGB bottom-up buffer.
//
// Open/Closed principle: add new codecs by implementing this interface
// and registering them in CreateFrameDecoder(), without modifying existing
// decoder code.
// ---------------------------------------------------------------------------

class IFrameDecoder
{
public:
    virtual ~IFrameDecoder() = default;

    /// Decode one frame.
    ///
    /// @param data             Pointer to the compressed frame payload.
    /// @param dataSize         Size of the compressed payload in bytes.
    /// @param width            Expected output width in pixels.
    /// @param height           Expected output height in pixels.
    /// @param output           Destination buffer (pre-allocated, at least height * outputStride bytes).
    /// @param outputStride     Bytes per row in the destination buffer.
    ///
    /// The output must be **bottom-up** 32bpp ARGB (first row in memory = bottom
    /// scanline) with alpha set to 0xFF. This matches the engine's expectation
    /// for VxDoBlitUpsideDown.
    ///
    /// @return true on success.
    virtual bool Decode(const uint8_t *data, size_t dataSize,
                        int width, int height,
                        uint8_t *output, int outputStride) = 0;

    /// Reset internal state for codecs that keep frame history.
    virtual void Reset() {}

    /// True if this decoder requires sequential decode order.
    virtual bool NeedsSequentialFrames() const { return false; }
};

// ---------------------------------------------------------------------------
// Factory: create the appropriate decoder for a given codec FourCC.
// Returns nullptr if the codec is not supported.
// ---------------------------------------------------------------------------

std::unique_ptr<IFrameDecoder> CreateFrameDecoder(const avi::AviStreamInfo &info);

// ---------------------------------------------------------------------------
// Concrete decoders
// ---------------------------------------------------------------------------

// Convenience alias
#define FRAMEDECODER_DECODE_OVERRIDE \
    bool Decode(const uint8_t *data, size_t dataSize, \
                int width, int height, \
                uint8_t *output, int outputStride) override

/// Decodes uncompressed/bitfield DIB frames (8/16/24/32 bpp) to 32bpp ARGB bottom-up.
class RawFrameDecoder : public IFrameDecoder
{
public:
    explicit RawFrameDecoder(const avi::AviStreamInfo &info);

    FRAMEDECODER_DECODE_OVERRIDE;

private:
    int m_SrcBpp;
    bool m_SrcTopDown;
    uint32_t m_RedMask;
    uint32_t m_GreenMask;
    uint32_t m_BlueMask;
    uint32_t m_AlphaMask;
    std::vector<uint32_t> m_Palette;
};

/// Decodes MJPEG frames using stb_image, outputs 32bpp ARGB bottom-up.
class MjpegFrameDecoder : public IFrameDecoder
{
public:
    FRAMEDECODER_DECODE_OVERRIDE;
};

// ---------------------------------------------------------------------------
// DeltaFrameDecoder -- base for codecs that maintain an inter-frame buffer
// (e.g. RLE8, Microsoft Video 1). Owns the previous-frame state and
// implements Reset() / NeedsSequentialFrames().
// ---------------------------------------------------------------------------
class DeltaFrameDecoder : public IFrameDecoder
{
public:
    void Reset() override;
    bool NeedsSequentialFrames() const override { return true; }

protected:
    /// Ensure m_PreviousFrame is sized for (width, height, outputStride), copy it
    /// to output, and return frameBytes. Returns 0 if allocation fails.
    size_t BeginFrame(uint8_t *output, int width, int height, int outputStride);

    /// Copy output back to m_PreviousFrame after a successful decode.
    void CommitFrame(const uint8_t *output, size_t frameBytes);

    std::vector<uint8_t> m_PreviousFrame;
    int m_Width  = 0;
    int m_Height = 0;
};

/// Decodes BI_RLE8 streams (8bpp indexed) to 32bpp ARGB bottom-up.
class Rle8FrameDecoder : public DeltaFrameDecoder
{
public:
    explicit Rle8FrameDecoder(const avi::AviStreamInfo &info);

    FRAMEDECODER_DECODE_OVERRIDE;

private:
    bool m_SrcTopDown;
    std::vector<uint32_t> m_Palette;
};

/// Decodes packed YUV 4:2:2 streams (YUY2/UYVY) to 32bpp ARGB bottom-up.
class PackedYuv422FrameDecoder : public IFrameDecoder
{
public:
    PackedYuv422FrameDecoder(bool srcTopDown, bool uyvyLayout);

    FRAMEDECODER_DECODE_OVERRIDE;

private:
    bool m_SrcTopDown;
    bool m_Uyvy;
};

/// Decodes Microsoft Video 1 / CRAM frames to 32bpp ARGB bottom-up.
class Msvideo1FrameDecoder : public DeltaFrameDecoder
{
public:
    FRAMEDECODER_DECODE_OVERRIDE;
};

#endif // FRAMEDECODER_H
