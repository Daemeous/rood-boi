/* The parity gate's walker and comparison (pc/src/classic/classic_parity.c)
 * on synthetic packets: a string printed as the game's raw printer does (a
 * draw-mode packet, a sprite per glyph, a zero-length tail) linked at an
 * ordering-table slot's head; walked in drawing order, kept as words,
 * emitted again into another buffer and walked equal (the round trip); each
 * kind of difference named by its field; the scan in allocation order; the
 * emit-only suppression; the JSON dump. */
#include "classic/classic_parity.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                         \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* Guest memory: links are word offsets into `ram` from BASE. */
#define BASE 0x100000u
static unsigned ram[1024];
static unsigned link_of(const unsigned* p) { return BASE + (unsigned)(p - ram) * 4; }
static const unsigned* resolve(unsigned link, void* user)
{
    (void)user;
    link &= 0xFFFFFF;
    if (link == 0xFFFFFF || link < BASE || link >= BASE + sizeof ram)
        return NULL;
    return &ram[(link - BASE) / 4];
}
static int anchor_value = 1;
static int anchor(const unsigned* tag, void* user)
{
    (void)tag, (void)user;
    return anchor_value;
}
static const pc_classic_walker walker = { resolve, anchor, NULL };

/* A packet of n words at *at, linked at the head of slot (prepended), as AddPrim. */
static unsigned* add(unsigned** at, unsigned* slot, int n)
{
    unsigned* p = *at;
    p[0] = (*slot & 0xFFFFFF) | ((unsigned)n << 24);
    *slot = link_of(p) & 0xFFFFFF;
    *at = p + 1 + n;
    return p + 1;
}

/* "SLOW!" as vs_battle_renderTextRawColor builds it: a chain, not AddPrim per glyph. */
static unsigned* print(unsigned* at, unsigned* slot, int x, int y, unsigned colour)
{
    static const int u[5] = { 172, 140, 168, 200, 4 }, w[5] = { 7, 7, 8, 9, 3 };
    unsigned *first = at, *p = at;
    p[0] = (link_of(p + 2) & 0xFFFFFF) | 0x01000000u;
    p[1] = 0xE100000Cu;
    p += 2;
    for (int i = 0; i < 5; i++) {
        p[0] = (link_of(p + 5) & 0xFFFFFF) | 0x04000000u;
        p[1] = colour | 0x64000000u;
        p[2] = (unsigned)(x & 0xFFFF) | (unsigned)y << 16;
        p[3] = (unsigned)u[i] | 76u << 8 | 0x37F60000u;
        p[4] = (unsigned)w[i] | 0xA0000u;
        p += 5;
        x += w[i];
    }
    *p = *slot & 0xFFFFFF; /* the tail: the slot's old head */
    *slot = link_of(first) & 0xFFFFFF;
    return p + 1;
}

int main(void)
{
    pc_classic_packet game[32], lua[32], again[32];
    pc_classic_diff d;
    unsigned slot = 0xFFFFFF, shadow = 0xFFFFFF;

    /* something older in the slot first (outside the range) */
    unsigned* at = ram;
    unsigned* old = add(&at, &slot, 3);
    old[0] = 0x60404040u, old[1] = 0, old[2] = 0x00100010u;
    unsigned* lo = at;
    unsigned* hi = print(at, &slot, 44, 48, 0x408040u);
    int n = pc_classic_walk(&walker, &slot, lo, hi, game, 32);
    CHECK(n == 6); /* the draw mode and five glyphs: the tail and the older packet left out */
    CHECK(game[0].words == 1 && game[0].w[0] == 0xE100000Cu);
    CHECK(game[1].words == 4 && (game[1].w[0] >> 24) == 0x64 && game[1].w[1] == (44u | 48u << 16));
    CHECK(game[5].w[1] == ((44u + 7 + 7 + 8 + 9) | 48u << 16));
    CHECK(game[2].anchor == 1 && game[0].at == 0 && game[1].at == 2);

    /* the round trip: each kept packet emitted again elsewhere (last first, linked at a shadow
     * slot's head, as a classic draw does) and walked equal */
    unsigned* lo2 = ram + 512;
    at = lo2;
    for (int i = n - 1; i >= 0; i--) {
        unsigned* p = add(&at, &shadow, game[i].words);
        memcpy(p, game[i].w, sizeof(unsigned) * game[i].words);
    }
    int m = pc_classic_walk(&walker, &shadow, lo2, at, lua, 32);
    CHECK(m == n);
    CHECK(pc_classic_compare(game, n, lua, m, &d) == 0 && d.index == -1 && d.fields == 0);

    /* each difference named */
    memcpy(again, lua, sizeof lua);
    again[3].w[1] += 1; /* a glyph one pixel right */
    CHECK(pc_classic_compare(game, n, again, m, &d) == 1 && d.index == 3 && d.word == 1
        && d.field == PC_CLASSIC_FIELD_XY && d.game == game[3].w[1] && d.lua == game[3].w[1] + 1);
    memcpy(again, lua, sizeof lua);
    again[2].w[2] ^= 0x00010000u; /* another CLUT */
    CHECK(pc_classic_compare(game, n, again, m, &d) == 1 && d.field == PC_CLASSIC_FIELD_CLUT);
    memcpy(again, lua, sizeof lua);
    again[2].w[2] ^= 0x2u; /* other texels */
    CHECK(pc_classic_compare(game, n, again, m, &d) == 1 && d.field == PC_CLASSIC_FIELD_UV);
    memcpy(again, lua, sizeof lua);
    again[4].w[0] ^= 0x02000000u; /* semi-transparent */
    CHECK(pc_classic_compare(game, n, again, m, &d) == 1 && d.field == PC_CLASSIC_FIELD_CODE);
    memcpy(again, lua, sizeof lua);
    again[4].w[0] ^= 0x10u; /* its colour */
    CHECK(pc_classic_compare(game, n, again, m, &d) == 1 && d.field == PC_CLASSIC_FIELD_COLOUR);
    memcpy(again, lua, sizeof lua);
    again[5].w[3] += 1; /* a wider sprite */
    CHECK(pc_classic_compare(game, n, again, m, &d) == 1 && d.field == PC_CLASSIC_FIELD_WH);
    memcpy(again, lua, sizeof lua);
    again[0].w[0] = 0xE100002Cu; /* another draw mode */
    CHECK(pc_classic_compare(game, n, again, m, &d) == 1 && d.field == PC_CLASSIC_FIELD_MODE && d.index == 0);
    memcpy(again, lua, sizeof lua);
    again[1].anchor = 2; /* anchored at the other edge */
    CHECK(pc_classic_compare(game, n, again, m, &d) == 1 && d.field == PC_CLASSIC_FIELD_ANCHOR && d.index == 1);
    CHECK(pc_classic_compare(game, n, lua, m - 1, &d) == 1 && d.field == PC_CLASSIC_FIELD_COUNT && d.index == m - 1
        && d.game == (unsigned)n && d.lua == (unsigned)m - 1);
    memcpy(again, lua, sizeof lua);
    again[3].w[1] += 1, again[5].w[2] ^= 0x00010000u; /* two fields: both reported, the first first */
    CHECK(pc_classic_compare(game, n, again, m, &d) == 1 && d.field == PC_CLASSIC_FIELD_XY
        && d.fields == (1u << PC_CLASSIC_FIELD_XY | 1u << PC_CLASSIC_FIELD_CLUT));
    CHECK(!strcmp(pc_classic_field_name(PC_CLASSIC_FIELD_TPAGE), "tpage"));

    /* allocation order: the draw mode, the glyphs (the tail has no length) */
    int s = pc_classic_scan(&walker, lo, hi, again, 32);
    CHECK(s == n && again[1].w[1] == game[1].w[1]);

    /* emit-only suppression: the glyphs become no-ops, the draw mode stays; the walk then sees only it */
    CHECK(pc_classic_suppress(lo, hi) == 5);
    CHECK(pc_classic_walk(&walker, &slot, lo, hi, again, 32) == 1 && again[0].w[0] == 0xE100000Cu);
    /* a sprite packet as the game's (its draw mode, then the sprite) draws: it goes too */
    unsigned* sp = ram + 900;
    sp[0] = 0x05000000u, sp[1] = 0xE100000Cu, sp[2] = 0x64808080u, sp[3] = 0x000A000Au, sp[4] = 0x37F60090u,
    sp[5] = 0x00090010u;
    CHECK(pc_classic_suppress(sp, sp + 6) == 1 && (sp[0] >> 24) == 0);

    /* the dump */
    char json[4096];
    int len = pc_classic_packets_json(game, n, json, sizeof json);
    CHECK(len > 0 && (size_t)len == strlen(json));
    CHECK(strstr(json, "\"kind\":\"mode\"") && strstr(json, "\"kind\":\"sprite\"") && strstr(json, "\"xy\":[[44,48]]")
        && strstr(json, "\"wh\":[7,10]") && strstr(json, "\"colour\":\"#408040\"") && strstr(json, "\"clut\":[864,223]"));
    CHECK(pc_classic_packets_json(game, n, json, 64) <= 63 && strlen(json) <= 63); /* truncated, terminated */

    /* names */
    CHECK(pc_classic_kind_of("status_panel") == PC_CLASSIC_STATUS_PANEL && !pc_classic_kind_of("nope")
        && !strcmp(pc_classic_kind_name(PC_CLASSIC_CHAIN), "chain"));

    if (failures)
        return 1;
    printf("classic_parity_probe: ok\n");
    return 0;
}
