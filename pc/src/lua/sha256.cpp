// SHA-256 (FIPS 180-4), written for this project (CC0 like the rest of
// pc/): plugins' content hashes.
#include "sha256.h"

#include <cstdio>
#include <cstring>

namespace rl {
namespace {
const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};
inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
} // namespace

void Sha256::block(const unsigned char* p)
{
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

void Sha256::update(const void* data, size_t size)
{
    const unsigned char* p = (const unsigned char*)data;
    total += size;
    while (size) {
        size_t take = 64 - used < size ? 64 - used : size;
        std::memcpy(buffer + used, p, take);
        used += take, p += take, size -= take;
        if (used == 64) {
            block(buffer);
            used = 0;
        }
    }
}

std::string Sha256::hex()
{
    uint64_t bits = total * 8;
    unsigned char pad = 0x80, zero = 0, length[8];
    update(&pad, 1);
    while (used != 56)
        update(&zero, 1);
    for (int i = 0; i < 8; i++)
        length[i] = (unsigned char)(bits >> (56 - 8 * i));
    update(length, 8);
    char out[65];
    for (int i = 0; i < 8; i++)
        std::snprintf(out + 8 * i, 9, "%08x", (unsigned)h[i]);
    return std::string(out, 64);
}

std::string sha256_hex(const void* data, size_t size)
{
    Sha256 s;
    s.update(data, size);
    return s.hex();
}
} // namespace rl
