/* Overlay statics: restart an overlay's native data on every load.
 *
 * On the PS1 an overlay's statics live inside its image, so each disc load
 * (e.g. MAINMENU.PRG at every menu open) brings them back to their initial
 * values. Natively compiled, they sit in host .data/.bss and would persist.
 * build_boot.py moves each overlay's .data/.bss into its own sections and
 * generates pc_overlay_statics[] from their bounds; here the initial .data
 * is snapshotted at startup and restored (and .bss cleared) whenever the
 * disc read that loads that overlay file is serviced. ROOD_OVERLAY_STATICS=0
 * turns this off (natively persistent statics, for comparison). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct pc_overlay_statics {
    const char* path; /* file on the disc, e.g. "MENU/MAINMENU.PRG" */
    char *data_start, *data_end, *bss_start, *bss_end;
};
extern const struct pc_overlay_statics pc_overlay_statics[];
extern const int pc_overlay_statics_count;
extern void pc_unimplemented(const char*) __attribute__((noreturn));

static struct {
    int lba;
    char* snapshot;
} * loaded;

static int read_sector(FILE* f, int lba, unsigned char* out)
{
    unsigned char sector[2352];
    if (fseek(f, (long)lba * 2352, SEEK_SET) || fread(sector, 1, sizeof sector, f) != sizeof sector
        || (sector[15] != 1 && sector[15] != 2))
        return 0;
    memcpy(out, sector + (sector[15] == 2 ? 24 : 16), 2048);
    return 1;
}

static unsigned le32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }

/* ISO 9660 lookup of a '/'-separated path; returns its first LBA or -1. */
static int find_file(FILE* f, const char* path)
{
    unsigned char block[2048];
    if (!read_sector(f, 16, block) || block[0] != 1 || memcmp(block + 1, "CD001", 5))
        return -1;
    unsigned lba = le32(block + 156 + 2), size = le32(block + 156 + 10);
    for (;;) {
        const char* slash = strchr(path, '/');
        size_t len = slash ? (size_t)(slash - path) : strlen(path);
        int found = 0;
        for (unsigned offset = 0; offset < size && !found; offset += 2048) {
            if (!read_sector(f, lba + offset / 2048, block))
                return -1;
            for (unsigned i = 0; i < 2048 && block[i] && !found; i += block[i]) {
                const unsigned char* record = block + i;
                unsigned name_len = record[32];
                const char* name = (const char*)record + 33;
                if (name_len >= len && !memcmp(name, path, len)
                    && (name_len == len || name[len] == ';')) {
                    lba = le32(record + 2);
                    size = le32(record + 10);
                    found = 1;
                }
            }
        }
        if (!found)
            return -1;
        if (!slash)
            return lba;
        path = slash + 1;
    }
}

void pc_overlay_statics_init(void)
{
    const char* enabled = getenv("ROOD_OVERLAY_STATICS");
    if (enabled && !strcmp(enabled, "0")) {
        fprintf(stderr, "[overlay] statics restart disabled (ROOD_OVERLAY_STATICS=0)\n");
        return;
    }
    loaded = calloc(pc_overlay_statics_count ? pc_overlay_statics_count : 1, sizeof *loaded);
    FILE* f = fopen("disks/SLUS-01040.bin", "rb");
    if (!loaded || !f) {
        perror("overlay statics");
        exit(1);
    }
    size_t bytes = 0;
    for (int i = 0; i < pc_overlay_statics_count; i++) {
        const struct pc_overlay_statics* o = &pc_overlay_statics[i];
        loaded[i].lba = find_file(f, o->path);
        if (loaded[i].lba < 0) {
            fprintf(stderr, "[overlay] %s not found on the disc\n", o->path);
            pc_unimplemented("overlay file lookup");
        }
        /* Byte-identical overlay copies share one region: share its snapshot. */
        for (int j = 0; j < i && !loaded[i].snapshot; j++)
            if (pc_overlay_statics[j].data_start == o->data_start)
                loaded[i].snapshot = loaded[j].snapshot;
        if (!loaded[i].snapshot && o->data_end > o->data_start) {
            loaded[i].snapshot = malloc(o->data_end - o->data_start);
            if (!loaded[i].snapshot)
                pc_unimplemented("overlay statics snapshot");
            memcpy(loaded[i].snapshot, o->data_start, o->data_end - o->data_start);
            bytes += o->data_end - o->data_start;
        }
    }
    fclose(f);
    fprintf(stderr, "[overlay] %d overlay files, %zu bytes of initial statics\n",
        pc_overlay_statics_count, bytes);
}

/* A disc read starting at `lba` was serviced: restart the statics of the
 * overlay file that begins there, as reloading its image does on the PS1. */
void pc_overlay_statics_loaded(int lba)
{
    if (!loaded)
        return;
    for (int i = 0; i < pc_overlay_statics_count; i++) {
        const struct pc_overlay_statics* o = &pc_overlay_statics[i];
        if (loaded[i].lba != lba)
            continue;
        if (o->data_end > o->data_start)
            memcpy(o->data_start, loaded[i].snapshot, o->data_end - o->data_start);
        memset(o->bss_start, 0, o->bss_end - o->bss_start);
    }
}
