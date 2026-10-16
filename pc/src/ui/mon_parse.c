/* Modern menus, the native Score screen's Bestiary file (mon_parse.h). */
#include "mon_parse.h"
#include <string.h>

static int s16(const unsigned char* p) { return (short)(unsigned short)(p[0] | p[1] << 8); }

int pc_mon_unlocked(int offset, int count, const int kill_flags[PC_MON_FLAG_WORDS])
{
    int j;
    /* outside the 832 flags: locked (the original would read past the array) */
    if (offset < 0 || count < 0 || offset + count > PC_MON_FLAG_WORDS * 32)
        return 0;
    for (j = offset; j < offset + count; ++j)
        if ((unsigned)kill_flags[j / 32] & (1u << (j % 32)))
            return 1;
    return 0;
}

/* Bytes of the string at `at` up to its 0xE7 (excluded), at most `max`; -1 when none within. */
static int terminated(const unsigned char* data, unsigned size, unsigned at, unsigned max)
{
    unsigned i;
    for (i = 0; i < max && at + i < size; ++i)
        if (data[at + i] == 0xE7)
            return (int)i;
    return -1;
}

void pc_mon_parse(const unsigned char* data, unsigned size, const int kill_flags[PC_MON_FLAG_WORDS], pc_mon_file* out)
{
    const unsigned table = PC_MON_RECORDS * PC_MON_RECORD;
    int i, first = -1, last = -1, prev;
    memset(out, 0, sizeof *out);
    for (i = 0; i < PC_MON_ENEMIES; ++i)
        out->e[i].prev = out->e[i].next = out->e[i].cls = -1;
    if (!data || size < table + 2) {
        out->why = !data ? "MON.BIN not read" : "MON.BIN too short";
        return;
    }
    for (i = 0; i < PC_MON_ENEMIES; ++i) {
        const unsigned char* r = data + i * PC_MON_RECORD;
        pc_mon_entry* e = &out->e[i];
        unsigned name = (unsigned)(i * PC_MON_RECORD + 16);
        int n, cls = s16(r + 2);
        e->zud = s16(r);
        e->cls = cls >= 0 && cls < PC_MON_CLASSES ? cls : -1;
        e->flags_offset = s16(r + 4);
        e->flags_count = s16(r + 6);
        e->unlocked = pc_mon_unlocked(e->flags_offset, e->flags_count, kill_flags);
        n = terminated(data, size, name, PC_MON_NAME);
        e->name_at = (int)name;
        e->name_len = n < 0 ? PC_MON_NAME : n; /* unterminated: the field's 28 bytes */
        /* the description: the table's u16 offset (in u16 units from the table) */
        if (table + 2u * (unsigned)i + 2 <= size) {
            unsigned off = (unsigned)(data[table + 2 * i] | data[table + 2 * i + 1] << 8);
            unsigned at = table + 2u * off;
            int len = at < size ? terminated(data, size, at, size - at) : -1;
            if (len >= 0)
                e->desc_at = (int)at, e->desc_len = len;
        }
        if (e->unlocked) {
            ++out->unlocked;
            if (first < 0)
                first = i;
            last = i;
        }
    }
    /* the ring (_initEnemyList: prev the last unlocked before, the first's prev the last; next likewise) */
    prev = last;
    for (i = 0; i < PC_MON_ENEMIES; ++i)
        if (out->e[i].unlocked) {
            out->e[i].prev = prev;
            if (prev >= 0)
                out->e[prev].next = i;
            prev = i;
        }
    if (last >= 0)
        out->e[last].next = first;
    out->ok = 1;
}
