#include "vs_encode.h"
#include <stdint.h>

/* Inverse of pc_ui_text_decode's charset (pc/src/ui_text.c:73-86).
 * Keep the table positions, including unmapped pictographs; the probe checks
 * every mapped byte against the real decoder, not a second test decoder. */
static const uint16_t charset[189] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
    'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
    'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 0x152, 0xC0,
    0xC1, 0xC2, 0xC4, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF, 0xD2, 0xD3, 0xD4, 0xD6,
    0xD9, 0xDA, 0xDB, 0xDC, 0xDF, 0x153, 0xE0, 0xE1, 0xE2, 0xE4, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC,
    0xED, 0xEE, 0xEF, 0xF2, 0xF3, 0xF4, 0xF6, 0xF9, 0xFA, 0xFB, 0xFC, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0x201E, 0, 0, 0, 0, 0, 0xB7, 0x2014, 0, 0,
    '!', '"', '#', '$', '%', '&', '\'', '(', ')', '=', '@', '[', ']', ';', ':', ',',
    '.', '/', '\\', '<', '>', '?', '_', '-', '+', '*', '`', '{', '}', 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, '~', 0, 0, 0
};

static unsigned read_utf8(const unsigned char** text)
{
    const unsigned char* s = *text;
    unsigned cp = s[0];
    int n = cp >= 0xC2 && cp <= 0xDF ? 2
        : cp >= 0xE0 && cp <= 0xEF ? 3
        : cp >= 0xF0 && cp <= 0xF4 ? 4 : 0;
    *text = s + 1;
    if (cp < 0x80)
        return cp;
    if (!n)
        return 0xFFFD;
    cp &= (1u << (7 - n)) - 1;
    for (int i = 1; i < n; ++i) {
        if ((s[i] & 0xC0) != 0x80)
            return 0xFFFD;
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    if ((n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000)
        || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF)
        return 0xFFFD;
    *text = s + n;
    return cp;
}

int vs_encode(const char* utf8, unsigned char* out, int cap, int* bad)
{
    const unsigned char* s = (const unsigned char*)utf8;
    int n = 0;
    if (bad)
        *bad = 0;
    if (!out || cap <= 0)
        return 0;
    while (s && *s && n < cap - 1) {
        unsigned cp = read_utf8(&s);
        int code = -1;
        if (cp == ' ') {
            code = 0x8F;
        } else {
            for (int i = 0; i < (int)(sizeof charset / sizeof charset[0]); ++i) {
                if (charset[i] == cp) {
                    code = i;
                    break;
                }
            }
        }
        if (code < 0) {
            code = 0xA5;
            if (bad)
                ++*bad;
        }
        out[n++] = (unsigned char)code;
    }
    out[n++] = 0xE7;
    return n;
}
