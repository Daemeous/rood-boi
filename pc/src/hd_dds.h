// DDS base-level decoder. Mips are deliberately ignored by the nearest-sampled
// atlas. Block decoding: bcdec (MIT, see
// pc/third_party/texture_formats/bcdec.h).
#pragma once
#define BCDEC_STATIC
#define BCDEC_IMPLEMENTATION
#include "../third_party/texture_formats/bcdec.h"
#include <fstream>
static SDL_Surface *hd_load_dds(const char *path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    auto length = stream.tellg();
    if (length < 128 || length > 300 * 1024 * 1024)
        return nullptr;
    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    stream.seekg(0);
    if (!stream.read((char *)bytes.data(), bytes.size()))
        return nullptr;
    auto u32 = [&](size_t o) {
        return uint32_t(bytes[o]) | uint32_t(bytes[o + 1]) << 8 | uint32_t(bytes[o + 2]) << 16 |
               uint32_t(bytes[o + 3]) << 24;
    };
    if (u32(0) != 0x20534444 || u32(4) != 124 || u32(76) != 32)
        return nullptr;
    unsigned h = u32(12), w = u32(16), fourcc = u32(84), bits = u32(88);
    if (!w || !h || w > 8192 || h > 8192 || u32(24) > 1 || (u32(112) & 0x20fe00))
        return nullptr;
    unsigned offset = 128, format = 0, block = 0;
    // 1..5/7 are BC formats; 10=RGBA8, 11=BGRA8, 12=legacy bit masks.
    if (fourcc == 0x30315844) {
        if (bytes.size() < 148 || u32(132) != 3 || u32(140) != 1 || (u32(136) & 4))
            return nullptr;
        unsigned dxgi = u32(128);
        offset = 148;
        if (dxgi == 71 || dxgi == 72)
            format = 1;
        else if (dxgi == 74 || dxgi == 75)
            format = 2;
        else if (dxgi == 77 || dxgi == 78)
            format = 3;
        else if (dxgi == 80)
            format = 4;
        else if (dxgi == 83)
            format = 5;
        else if (dxgi == 98 || dxgi == 99)
            format = 7;
        else if (dxgi == 28 || dxgi == 29) {
            format = 10;
            bits = 32;
        } else if (dxgi == 87 || dxgi == 91) {
            format = 11;
            bits = 32;
        } else
            return nullptr;
    } else if (fourcc == 0x31545844)
        format = 1; // DXT1
    else if (fourcc == 0x33545844)
        format = 2;
    else if (fourcc == 0x35545844)
        format = 3;
    else if (fourcc == 0x31495441 || fourcc == 0x55344342)
        format = 4;
    else if (fourcc == 0x32495441 || fourcc == 0x55354342)
        format = 5;
    else if (!fourcc && (bits == 16 || bits == 24 || bits == 32) && (u32(80) & 0x40))
        format = 12;
    else
        return nullptr;
    if (format <= 7)
        block = (format == 1 || format == 4) ? 8 : 16;
    size_t pitch = block ? ((w + 3) / 4) * block : size_t(w) * (bits / 8);
    if (!block && (u32(8) & 8) && u32(20) >= pitch)
        pitch = u32(20);
    size_t needed = block ? pitch * ((h + 3) / 4) : pitch * h;
    if (needed > bytes.size() - offset)
        return nullptr;
    SDL_Surface *out = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32);
    if (!out)
        return nullptr;
    if (block) {
        for (unsigned y = 0; y < h; y += 4)
            for (unsigned x = 0; x < w; x += 4) {
                const uint8_t *src = bytes.data() + offset + (y / 4) * pitch + (x / 4) * block;
                uint8_t tile[64] = {};
                if (format == 1)
                    bcdec_bc1(src, tile, 16);
                else if (format == 2)
                    bcdec_bc2(src, tile, 16);
                else if (format == 3)
                    bcdec_bc3(src, tile, 16);
                else if (format == 7)
                    bcdec_bc7(src, tile, 16);
                else {
                    uint8_t channels[32] = {};
                    if (format == 4)
                        bcdec_bc4(src, channels, 4);
                    else
                        bcdec_bc5(src, channels, 8);
                    for (int i = 0; i < 16; ++i) {
                        tile[i * 4] = channels[i * (format == 4 ? 1 : 2)];
                        tile[i * 4 + 1] = format == 4 ? tile[i * 4] : channels[i * 2 + 1];
                        tile[i * 4 + 2] = format == 4 ? tile[i * 4] : 0;
                        tile[i * 4 + 3] = 255;
                    }
                }
                for (unsigned j = 0; j < 4 && y + j < h; ++j)
                    memcpy((uint8_t *)out->pixels + (y + j) * out->pitch + x * 4, tile + j * 16,
                           std::min(4u, w - x) * 4);
            }
    } else {
        auto channel = [](uint32_t pixel, uint32_t mask, unsigned fallback) -> uint8_t {
            if (!mask)
                return fallback;
            while (!(mask & 1)) {
                mask >>= 1;
                pixel >>= 1;
            }
            return uint64_t(pixel & mask) * 255 / mask;
        };
        for (unsigned y = 0; y < h; ++y)
            for (unsigned x = 0; x < w; ++x) {
                const uint8_t *src = bytes.data() + offset + y * pitch + x * (bits / 8);
                uint8_t *dst = (uint8_t *)out->pixels + y * out->pitch + x * 4;
                if (format == 10 || format == 11) {
                    dst[0] = src[format == 10 ? 0 : 2];
                    dst[1] = src[1];
                    dst[2] = src[format == 10 ? 2 : 0];
                    dst[3] = src[3];
                } else {
                    uint32_t pixel = 0;
                    for (unsigned n = 0; n < bits / 8; ++n)
                        pixel |= uint32_t(src[n]) << (n * 8);
                    for (int n = 0; n < 4; ++n)
                        dst[n] = channel(pixel, u32(92 + n * 4), n == 3 ? 255 : 0);
                }
            }
    }
    return out;
}
