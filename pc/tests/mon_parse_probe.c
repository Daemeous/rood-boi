/* The native Score screen's Bestiary file (pc/src/ui/mon_parse.c) on synthetic
 * MON.BIN images built here (never the disc's): truncated and oversized
 * files, offsets outside the kill flags, unterminated names and descriptions,
 * descriptions outside the file, classes out of range; the unlocked flags and
 * the prev / next ring against a transcription of MENU9's _initEnemyList
 * (src/MENU/MENU9.PRG/238.c) for no entry, one (itself), the first and the
 * last (the wrap), all 78, a flag at a word's last bit, every single-bit mask
 * and random masks. */
#include "mon_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...)                                                                                       \
    do {                                                                                                       \
        if (!(cond)) {                                                                                         \
            ++failures;                                                                                        \
            if (failures < 40) {                                                                               \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                    \
                printf(__VA_ARGS__);                                                                           \
                printf("\n");                                                                                  \
            }                                                                                                  \
        }                                                                                                      \
    } while (0)

enum { TABLE = PC_MON_RECORDS * PC_MON_RECORD };

/* 238.c's _monBinData_t, the fields _initEnemyList touches */
typedef struct {
    short killFlagsOffset, killFlagsCount;
    unsigned char unlocked, prev, next;
} ref_t;

/* _initEnemyList, transcribed (char index: signed on the PlayStation's compiler for this file) */
static void reference(ref_t* mon, const int* enemyKillFlags)
{
    int i;
    signed char index;
    ref_t* enemy;
    for (i = 0, enemy = mon; i < 78; ++i, ++enemy) {
        int j;
        enemy->unlocked = 0;
        for (j = enemy->killFlagsOffset; j < (enemy->killFlagsOffset + enemy->killFlagsCount); ++j) {
            int flag = enemyKillFlags[j / 32] & (1 << (j % 32));
            if (flag) {
                enemy->unlocked = 1;
                break;
            }
        }
    }
    for (i = 0, enemy = mon, index = -1; i < 78; ++i, ++enemy)
        if (enemy->unlocked != 0) {
            enemy->prev = (unsigned char)index;
            index = (signed char)i;
        }
    for (i = 0, enemy = mon; i < 78; ++i, ++enemy)
        if (enemy->unlocked != 0) {
            enemy->prev = (unsigned char)index;
            break;
        }
    for (i = 77, enemy = &mon[77], index = -1; i >= 0; --i, --enemy)
        if (enemy->unlocked != 0) {
            enemy->next = (unsigned char)index;
            index = (signed char)i;
        }
    for (i = 77, enemy = &mon[77]; i >= 0; --i, --enemy)
        if (enemy->unlocked != 0) {
            enemy->next = (unsigned char)index;
            break;
        }
}

static void put16(unsigned char* p, int v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

/* A file: entry i's flags at offset 2i + (i & 1) * 3, counts 1..4 (overlapping some), class i % 6, name
 * "\xFA\x0E" + 3 letters, descriptions 4 bytes each. */
static unsigned build(unsigned char* f, unsigned cap)
{
    unsigned at;
    int i;
    memset(f, 0, cap);
    for (i = 0; i < PC_MON_RECORDS; ++i) {
        unsigned char* r = f + i * PC_MON_RECORD;
        put16(r, 100 + i);
        put16(r + 2, i % 6);
        put16(r + 4, i < 78 ? 9 * i + (i & 1) * 3 : 0);
        put16(r + 6, i < 78 ? 1 + i % 4 : 0);
        r[16] = 0xFA, r[17] = 0x0E, r[18] = (unsigned char)(10 + i % 26), r[19] = 11, r[20] = 12, r[21] = 0xE7;
    }
    at = TABLE + PC_MON_RECORDS * 2;
    for (i = 0; i < PC_MON_RECORDS; ++i) {
        put16(f + TABLE + 2 * i, (int)(at - TABLE) / 2);
        f[at] = (unsigned char)(20 + i % 30), f[at + 1] = 21, f[at + 2] = 22, f[at + 3] = 0xE7;
        at += 4;
    }
    return at;
}

static void compare(const unsigned char* f, unsigned size, const int* flags, const char* what)
{
    pc_mon_file m;
    ref_t ref[78];
    int i, unlocked = 0;
    memset(ref, 0xEE, sizeof ref); /* locked entries keep the file's bytes (here 0xEE: never compared) */
    for (i = 0; i < 78; ++i) {
        ref[i].killFlagsOffset = (short)(f[i * 44 + 4] | f[i * 44 + 5] << 8);
        ref[i].killFlagsCount = (short)(f[i * 44 + 6] | f[i * 44 + 7] << 8);
    }
    reference(ref, flags);
    pc_mon_parse(f, size, flags, &m);
    CHECK(m.ok, "%s: parsed", what);
    for (i = 0; i < 78; ++i) {
        CHECK(m.e[i].unlocked == ref[i].unlocked, "%s: entry %d unlocked %d, original %d", what, i, m.e[i].unlocked,
            ref[i].unlocked);
        if (ref[i].unlocked) {
            ++unlocked;
            CHECK(m.e[i].prev == ref[i].prev && m.e[i].next == ref[i].next, "%s: entry %d prev/next %d/%d, original %d/%d",
                what, i, m.e[i].prev, m.e[i].next, ref[i].prev, ref[i].next);
        } else {
            CHECK(m.e[i].prev == -1 && m.e[i].next == -1, "%s: locked entry %d has no ring", what, i);
        }
    }
    CHECK(m.unlocked == unlocked, "%s: %d unlocked, original %d", what, m.unlocked, unlocked);
}

int main(void)
{
    static unsigned char f[16384];
    unsigned size = build(f, sizeof f);
    int flags[PC_MON_FLAG_WORDS];
    pc_mon_file m;
    int i;

    /* fields */
    memset(flags, 0, sizeof flags);
    pc_mon_parse(f, size, flags, &m);
    CHECK(m.ok && m.unlocked == 0, "the synthetic file parses, nothing unlocked");
    CHECK(m.e[5].zud == 105 && m.e[5].cls == 5 && m.e[5].flags_offset == 48 && m.e[5].flags_count == 2,
        "entry 5's fields");
    CHECK(m.e[3].name_at == 3 * 44 + 16 && m.e[3].name_len == 5, "entry 3's name: %d %d", m.e[3].name_at, m.e[3].name_len);
    CHECK(m.e[3].desc_len == 3 && f[m.e[3].desc_at] == 23, "entry 3's description");
    for (i = 0; i < 78; ++i)
        CHECK(m.e[i].prev == -1 && m.e[i].next == -1, "none unlocked: entry %d has no ring", i);

    /* the rings */
    compare(f, size, flags, "none");
    flags[0] = 1; /* entry 0 (offset 0) */
    compare(f, size, flags, "one");
    pc_mon_parse(f, size, flags, &m);
    CHECK(m.e[0].prev == 0 && m.e[0].next == 0, "one: itself");
    memset(flags, 0, sizeof flags);
    {
        int j77 = 9 * 77 + 3; /* entry 77's first flag */
        flags[0] = 1;
        flags[j77 / 32] |= 1 << (j77 % 32);
    }
    compare(f, size, flags, "first and last");
    pc_mon_parse(f, size, flags, &m);
    CHECK(m.e[0].prev == 77 && m.e[77].next == 0, "first and last: the ring wraps");
    memset(flags, 0xFF, sizeof flags);
    compare(f, size, flags, "all");
    memset(flags, 0, sizeof flags);
    for (i = 0; i < 78; ++i) { /* a flag at a word's last bit (j % 32 == 31) */
        int off = 9 * i + (i & 1) * 3, j;
        for (j = off; j < off + 1 + i % 4; ++j)
            if (j % 32 == 31) {
                flags[j / 32] = (int)(1u << 31);
                compare(f, size, flags, "bit 31");
                pc_mon_parse(f, size, flags, &m);
                CHECK(m.e[i].unlocked, "bit 31 of word %d unlocks entry %d", j / 32, i);
                flags[j / 32] = 0;
            }
    }
    /* every single bit, then random masks */
    for (i = 0; i < PC_MON_FLAG_WORDS * 32; ++i) {
        char what[32];
        memset(flags, 0, sizeof flags);
        flags[i / 32] = (int)(1u << (i % 32));
        snprintf(what, sizeof what, "bit %d", i);
        compare(f, size, flags, what);
    }
    srand(12345);
    for (int k = 0; k < 2000; ++k) {
        for (i = 0; i < PC_MON_FLAG_WORDS; ++i)
            flags[i] = (k & 1) ? (int)(((unsigned)rand() << 16) ^ (unsigned)rand()) & rand() & rand()
                               : (rand() % 7 == 0 ? (int)(1u << (rand() % 32)) : 0);
        compare(f, size, flags, "random");
    }

    /* bounds */
    memset(flags, 0xFF, sizeof flags);
    pc_mon_parse(f, TABLE + 1, flags, &m);
    CHECK(!m.ok && m.why && m.unlocked == 0, "truncated: refused (%s)", m.why ? m.why : "");
    for (i = 0; i < 78; ++i)
        CHECK(m.e[i].prev == -1 && m.e[i].next == -1 && !m.e[i].unlocked, "truncated: entry %d empty", i);
    pc_mon_parse(NULL, 0, flags, &m);
    CHECK(!m.ok, "no data: refused");
    pc_mon_parse(f, TABLE + 2, flags, &m); /* the table's first offset only */
    CHECK(m.ok && m.e[0].desc_len == 0 && m.e[1].desc_len == 0, "a table cut short: no descriptions");
    pc_mon_parse(f, sizeof f, flags, &m); /* oversized: zeros after */
    CHECK(m.ok && m.e[77].desc_len == 3, "oversized: parsed");
    {
        static unsigned char g[16384];
        memcpy(g, f, sizeof g);
        put16(g + 4 * 44 + 4, 830), put16(g + 4 * 44 + 6, 5); /* past the 832 flags */
        put16(g + 6 * 44 + 4, -3), put16(g + 6 * 44 + 6, 2);  /* negative */
        put16(g + 7 * 44 + 2, 6);                             /* class 6 */
        put16(g + 8 * 44 + 2, -1);                            /* class -1 */
        memset(g + 9 * 44 + 16, 0x11, 28);                    /* an unterminated name */
        put16(g + TABLE + 2 * 10, 0x7000);                    /* a description outside the file */
        g[size - 1] = 0x22;                                   /* the last description unterminated */
        pc_mon_parse(g, size, flags, &m);
        CHECK(m.ok && !m.e[4].unlocked && !m.e[6].unlocked, "offsets outside the flags: locked");
        CHECK(m.e[7].cls == -1 && m.e[8].cls == -1 && m.e[5].cls == 5, "classes out of range: -1");
        CHECK(m.e[9].name_len == 28, "an unterminated name: the field's 28 bytes");
        CHECK(m.e[10].desc_len == 0 && m.e[10].desc_at == 0, "a description outside the file: none");
        CHECK(m.e[77].desc_len == 3, "entry 77's description kept");
        /* the ring skips the locked */
        for (i = 0; i < 78; ++i)
            if (m.e[i].unlocked)
                CHECK(m.e[m.e[i].next].unlocked && m.e[m.e[i].prev].unlocked && m.e[m.e[i].next].prev == i,
                    "the ring links unlocked entries (%d)", i);
    }
    printf("mon_parse_probe: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
