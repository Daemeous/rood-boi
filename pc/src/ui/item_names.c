/* Item and material names from the disc (item_names.h). */
#include "item_names.h"
#include "../ui_text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECORDS 512
#define RECORD 24
#define MATERIAL_BASE 254 /* VS_ITEMNAME_BIN_INDEX_wood (build/assets/MENU/ITEMNAME.BIN.h) */
#define NAME_MAX_UTF8 72  /* 24 game bytes, at most 3 UTF-8 bytes each */

static unsigned char table[RECORDS * RECORD];
static char names[RECORDS][NAME_MAX_UTF8];
static int state; /* 0 not read yet, 1 read, -1 failed */

static int read_sector(FILE* f, unsigned lba, unsigned char* out)
{
    unsigned char sector[2352];
    if (fseek(f, (long)lba * 2352, SEEK_SET) || fread(sector, 1, sizeof sector, f) != sizeof sector
        || (sector[15] != 1 && sector[15] != 2))
        return 0;
    memcpy(out, sector + (sector[15] == 2 ? 24 : 16), 2048);
    return 1;
}

static unsigned le32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

/* ISO 9660 lookup of a '/'-separated path (as overlay_statics.c): its first
 * LBA and size, 0 when not found. */
static int find_file(FILE* f, const char* path, unsigned* out_lba, unsigned* out_size)
{
    unsigned char block[2048];
    if (!read_sector(f, 16, block) || block[0] != 1 || memcmp(block + 1, "CD001", 5))
        return 0;
    unsigned lba = le32(block + 156 + 2), size = le32(block + 156 + 10);
    for (;;) {
        const char* slash = strchr(path, '/');
        size_t len = slash ? (size_t)(slash - path) : strlen(path);
        int found = 0;
        for (unsigned offset = 0; offset < size && !found; offset += 2048) {
            if (!read_sector(f, lba + offset / 2048, block))
                return 0;
            for (unsigned i = 0; i < 2048 && block[i] && !found; i += block[i]) {
                const unsigned char* record = block + i;
                unsigned name_len = record[32];
                const char* name = (const char*)record + 33;
                if (name_len >= len && !memcmp(name, path, len) && (name_len == len || name[len] == ';')) {
                    lba = le32(record + 2);
                    size = le32(record + 10);
                    found = 1;
                }
            }
        }
        if (!found)
            return 0;
        if (!slash) {
            *out_lba = lba;
            *out_size = size;
            return 1;
        }
        path = slash + 1;
    }
}

static int load(void)
{
    if (state)
        return state > 0;
    state = -1;
    const char* image = getenv("ROOD_DISC");
    FILE* f = fopen(image && *image ? image : "disks/SLUS-01040.bin", "rb");
    unsigned lba, size;
    if (!f) {
        fprintf(stderr, "[ui] item names: cannot open the disc image\n");
        return 0;
    }
    if (find_file(f, "MENU/ITEMNAME.BIN", &lba, &size) && size >= sizeof table) {
        unsigned char block[2048];
        unsigned done = 0;
        state = 1;
        for (unsigned k = 0; done < sizeof table; ++k, done += 2048) {
            if (!read_sector(f, lba + k, block)) {
                state = -1;
                break;
            }
            unsigned n = sizeof table - done < 2048 ? sizeof table - done : 2048;
            memcpy(table + done, block, n);
        }
    }
    fclose(f);
    if (state < 0)
        fprintf(stderr, "[ui] item names: MENU/ITEMNAME.BIN not read from the disc\n");
    return state > 0;
}

const char* pc_ui_item_name(int id)
{
    if (id < 0 || id >= RECORDS || !load())
        return "";
    if (!names[id][0] && table[id * RECORD] != 0xE7)
        pc_ui_text_decode(table + id * RECORD, RECORD, names[id], NAME_MAX_UTF8);
    return names[id];
}

const char* pc_ui_material_name(int material)
{
    return material > 0 ? pc_ui_item_name(material - 1 + MATERIAL_BASE) : "";
}
