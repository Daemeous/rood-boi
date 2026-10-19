#include "vs_encode.h"
#include "ui_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

int main(void)
{
    unsigned char out[256];
    char text[512];
    int bad, mapped = 0;
    /* Surround each byte with A so the decoder doesn't trim spaces. Control
     * operands are zero; their non-character output is deliberately excluded. */
    for (int c = 0; c < 256; ++c) {
        unsigned char encoded[] = { 10, (unsigned char)c, 0, 10, 0xE7 };
        int size = 5;
        if (c < 0xEC || c == 0xFC) {
            encoded[2] = 10;
            encoded[3] = 0xE7;
            size = 4;
        }
        pc_ui_text_decode(encoded, size, text, sizeof text);
        if (c == 0xE7 || !strcmp(text, "AA"))
            continue;
        CHECK(vs_encode(text, out, sizeof out, &bad) == 4);
        CHECK(bad == 0 && out[0] == 10 && out[2] == 10 && out[3] == 0xE7);
        CHECK(out[1] == (c == 0xE8 || c == 0xFA ? 0x8F : c));
        ++mapped;
    }
    CHECK(mapped == 143);
    const char* words = "Options Abc XYZ 0123456789 !\"#$%&'()=@[];:,./\\<>?_-+*`{}~";
    int n = vs_encode(words, out, sizeof out, &bad);
    CHECK(n == (int)strlen(words) + 1 && bad == 0);
    pc_ui_text_decode(out, n, text, sizeof text);
    CHECK(!strcmp(text, words));
    CHECK(vs_encode("\xC5\x92\xE2\x80\x94\xC3\xA9", out, 4, &bad) == 4);
    CHECK(!bad && out[0] == 0x3E && out[1] == 0x8D && out[2] == 0x5C);
    CHECK(vs_encode("A\xF0\x9F\x98\x80\xE4\xB8\xAD\nB", out, 8, &bad) == 6);
    CHECK(bad == 3 && out[1] == 0xA5 && out[2] == 0xA5 && out[3] == 0xA5);
    const char* invalid[] = { "\xC0\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80", "\xE2\x82", "\xFF" };
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        n = vs_encode(invalid[i], out, sizeof out, &bad);
        CHECK(bad == (int)strlen(invalid[i]) && n == bad + 1);
        for (int j = 0; j < bad; ++j)
            CHECK(out[j] == 0xA5);
    }
    memset(out, 0xCC, sizeof out);
    CHECK(vs_encode("AB", out, 0, &bad) == 0 && out[0] == 0xCC && !bad);
    CHECK(vs_encode("AB", out, -1, &bad) == 0 && out[0] == 0xCC);
    CHECK(vs_encode("AB", NULL, 5, &bad) == 0);
    CHECK(vs_encode("AB", out, 1, &bad) == 1 && out[0] == 0xE7 && out[1] == 0xCC);
    CHECK(vs_encode("\xC3\xA9\xF0\x9F\x98\x80", out, 2, &bad) == 2);
    CHECK(!bad && out[0] == 0x5C && out[1] == 0xE7 && out[2] == 0xCC);
    CHECK(vs_encode("", out, 2, NULL) == 1 && out[0] == 0xE7);
    CHECK(vs_encode(NULL, out, 2, &bad) == 1 && !bad);
    puts("vs_encode_probe: OK");
    return 0;
}
