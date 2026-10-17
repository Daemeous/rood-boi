/* SHA-256 (FIPS 180-4): a plugin's content hash (lua_plugins.cpp), which
 * its grants follow (lua_caps.cpp). */
#ifndef ROOD_SHA256_H
#define ROOD_SHA256_H
#include <cstddef>
#include <cstdint>
#include <string>

namespace rl {
class Sha256 {
public:
    void update(const void* data, size_t size);
    std::string hex(); /* finishes: the object is spent */

private:
    void block(const unsigned char* p);
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    unsigned char buffer[64] = {};
    size_t used = 0;
    uint64_t total = 0;
};
std::string sha256_hex(const void* data, size_t size);
} // namespace rl
#endif
