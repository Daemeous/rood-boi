/* Item and material names, and the item help texts, from the disc (item_names.h). */
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

/* The whole of disc file `path` into a new buffer (*out, *out_size); 0 when
 * it cannot be read. */
static int read_file(const char* path, unsigned char** out, unsigned* out_size)
{
    const char* image = getenv("ROOD_DISC");
    FILE* f = fopen(image && *image ? image : "disks/SLUS-01040.bin", "rb");
    unsigned lba, size, done = 0;
    unsigned char block[2048];
    unsigned char* data;
    if (!f) {
        fprintf(stderr, "[ui] item text: cannot open the disc image\n");
        return 0;
    }
    if (!find_file(f, path, &lba, &size) || !size || !(data = malloc(size))) {
        fclose(f);
        return 0;
    }
    for (unsigned k = 0; done < size; ++k, done += 2048) {
        if (!read_sector(f, lba + k, block)) {
            free(data);
            fclose(f);
            return 0;
        }
        memcpy(data + done, block, size - done < 2048 ? size - done : 2048);
    }
    fclose(f);
    *out = data;
    *out_size = size;
    return 1;
}

static int load(void)
{
    unsigned char* data;
    unsigned size;
    if (state)
        return state > 0;
    state = -1;
    if (read_file("MENU/ITEMNAME.BIN", &data, &size)) {
        if (size >= sizeof table) {
            memcpy(table, data, sizeof table);
            state = 1;
        }
        free(data);
    }
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

/* MENU/ITEMHELP.BIN: vs_mainMenu_itemHelp, a u_short table of offsets (in
 * u_shorts from the start) to the texts; the first offset is where the texts
 * begin, so it also counts the entries. */
static unsigned char* help_data;
static unsigned help_size, help_count;
static char** help_utf8;
static int help_state; /* 0 not read yet, 1 read, -1 failed */

static int load_help(void)
{
    if (help_state)
        return help_state > 0;
    help_state = -1;
    if (read_file("MENU/ITEMHELP.BIN", &help_data, &help_size) && help_size >= 2) {
        help_count = help_data[0] | help_data[1] << 8;
        if (help_count * 2 <= help_size && (help_utf8 = calloc(help_count, sizeof *help_utf8)))
            help_state = 1;
    }
    if (help_state < 0)
        fprintf(stderr, "[ui] item help: MENU/ITEMHELP.BIN not read from the disc\n");
    return help_state > 0;
}

const char* pc_ui_item_help(int index)
{
    unsigned offset;
    if (index < 0 || !load_help() || (unsigned)index >= help_count)
        return "";
    if (!help_utf8[index]) {
        char buf[1024];
        offset = (help_data[index * 2] | help_data[index * 2 + 1] << 8) * 2u;
        buf[0] = 0;
        if (offset < help_size)
            pc_ui_text_decode(help_data + offset, (int)(help_size - offset), buf, sizeof buf);
        help_utf8[index] = strdup(buf);
        if (!help_utf8[index])
            return "";
    }
    return help_utf8[index];
}

int pc_ui_item_help_count(void) { return load_help() ? (int)help_count : 0; }

const unsigned char* pc_ui_item_name_bytes(int id)
{
    if (id < 0 || id >= RECORDS || !load())
        return NULL;
    return table + id * RECORD;
}

int pc_ui_disc_read(const char* path, unsigned char** out, unsigned* size) { return read_file(path, out, size); }
