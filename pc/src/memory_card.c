/* File-backed PS1 memory cards for the BIOS card and file calls the game
 * makes (libcard _card_*, open/read/write/close/rename/erase and
 * firstfile/nextfile on "bu00:" and "bu10:").
 *
 * Each slot is a 128 KiB PS1 card image, so saves can move between this port
 * and an emulator:
 *   slot 1: $ROOD_CARD1 or pc/save/card1.mcd (always inserted; created on
 *           the first write)
 *   slot 2: $ROOD_CARD2 or pc/save/card2.mcd (inserted only if it exists)
 * The format is recognised from the file's size and first bytes, not its name,
 * and a card is written back in the format it was read in:
 *   raw image (.mcd/.mcr/.srm/.ddf), 128 KiB starting "MC": read and written;
 *   DexDrive (.gme), a 3904-byte header "123-456-STD" in front: read and
 *     written (the header's copy of the directory is kept up to date);
 *   VGS/Connectix (.mem/.vgs), a 64-byte header "VgsM" in front: read and
 *     written (header kept);
 *   PSP/PS Vita (.vmp), a 128-byte header "\0PMV" in front: read only (the
 *     console checks a signature over the image that is not recomputed here).
 * Any other file is never written: the slot shows a blank card that refuses
 * saves, so a wrong path can't destroy it. A missing (or empty) file becomes a
 * raw image on the first write.
 * Every operation completes immediately; asynchronous ones raise their
 * completion event at once. */
#include "psx_types.h" /* psx_long: the SDK's 32-bit long on every build */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "platform.h"

extern void pc_deliver_event(psx_ulong event, psx_long spec);

#define CARD_SIZE 0x20000
#define BLOCK_SIZE 0x2000
#define FRAME_SIZE 0x80
#define SW_CARD 0xF4000001ul
#define HW_CARD 0xF0000011ul
#define EV_IOE 0x0004
#define EV_TIMEOUT 0x0100
#define EV_NEW 0x2000
#define FILE_WRITE 0x0002
#define FILE_CREATE 0x0200
#define FILE_ASYNC 0x8000

/* The SDK's DIRENTRY (kernel.h): the game reads its long members as 32-bit
 * (the LP64 build reads the SDK headers with long spelled int); next is a
 * native pointer, filled here and only followed by game code (runtime). */
struct DIRENTRY {
    char name[20];
    int32_t attr;
    int32_t size;
    struct DIRENTRY* next;
    int32_t head;
    char system[4];
};

enum card_format { FORMAT_RAW, FORMAT_GME, FORMAT_VGS, FORMAT_VMP, FORMAT_UNKNOWN };
static const struct {
    const char* name;
    unsigned header; /* bytes in front of the 128 KiB image */
    const char* magic;
    unsigned magic_size;
    int writable;
} formats[] = {
    [FORMAT_RAW] = { "raw card image", 0, "MC", 2, 1 },
    [FORMAT_GME] = { "DexDrive .gme", 0xF40, "123-456-STD", 11, 1 },
    [FORMAT_VGS] = { "VGS .mem/.vgs", 0x40, "VgsM", 4, 1 },
    [FORMAT_VMP] = { "PSP/PS Vita .vmp", 0x80, "\0PMV", 4, 0 },
};
#define MAX_HEADER 0xF40

static struct card {
    int loaded, present, writable;
    enum card_format format;
    char path[512];
    unsigned char header[MAX_HEADER];
    unsigned char image[CARD_SIZE];
} cards[2];

static struct {
    int used, slot, first, async;
    psx_long position, size;
} files[16];

/* Tests (dev_rpc.c "card"): data writes made (pc_psx_write calls that wrote),
 * writes refused as the Room Browser's warp refuses them, and a card that
 * reads as unformatted (_card_load raises EvSpNEW, as libcard does for a new
 * card, until the card is formatted). */
static unsigned card_writes;
static int fail_writes, unformatted;
unsigned pc_card_write_count(void) { return card_writes; }
void pc_card_test_flags(int fail, int new_card)
{
    if (fail >= 0)
        fail_writes = fail != 0;
    if (new_card >= 0)
        unformatted = new_card != 0;
}
void pc_card_test_state(int* fail, int* new_card)
{
    *fail = fail_writes;
    *new_card = unformatted;
}

static unsigned char* frame(struct card* card, int index) { return card->image + index * FRAME_SIZE; }
static void seal(unsigned char* f)
{
    unsigned char sum = 0;
    for (int i = 0; i < FRAME_SIZE - 1; i++)
        sum ^= f[i];
    f[FRAME_SIZE - 1] = sum;
}
static unsigned get32(unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static void put32(unsigned char* p, unsigned v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}
static unsigned get16(unsigned char* p) { return p[0] | p[1] << 8; }
static void put16(unsigned char* p, unsigned v)
{
    p[0] = v;
    p[1] = v >> 8;
}

static void free_entry(struct card* card, int block)
{
    unsigned char* entry = frame(card, block + 1);
    memset(entry, 0, FRAME_SIZE);
    put32(entry, 0xA0);
    put16(entry + 8, 0xFFFF);
    seal(entry);
}
static void format(struct card* card)
{
    memset(card->image, 0, CARD_SIZE);
    unsigned char* header = frame(card, 0);
    header[0] = 'M';
    header[1] = 'C';
    seal(header);
    for (int block = 0; block < 15; block++)
        free_entry(card, block);
    for (int i = 16; i < 36; i++) {
        unsigned char* spare = frame(card, i);
        put32(spare, 0xFFFFFFFF);
        put16(spare + 8, 0xFFFF);
        seal(spare);
    }
    memcpy(frame(card, 63), header, FRAME_SIZE);
}

/* Reads an open card file into card (header and image) and returns its
 * format: the one whose total size and magic match, FORMAT_UNKNOWN for
 * anything else. An empty file is a new raw image. */
static enum card_format load_image(struct card* card, FILE* f)
{
    long size;
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET))
        return FORMAT_UNKNOWN;
    if (size == 0) {
        format(card);
        return FORMAT_RAW;
    }
    for (int i = 0; i < FORMAT_UNKNOWN; i++) {
        unsigned header = formats[i].header;
        if (size != (long)(header + CARD_SIZE))
            continue;
        if (fread(card->header, 1, header, f) != header || fread(card->image, 1, CARD_SIZE, f) != CARD_SIZE)
            return FORMAT_UNKNOWN;
        if (memcmp(header ? card->header : card->image, formats[i].magic, formats[i].magic_size)
            || memcmp(card->image, "MC", 2))
            return FORMAT_UNKNOWN;
        return (enum card_format)i;
    }
    return FORMAT_UNKNOWN;
}

static struct card* slot_card(int slot)
{
    if (slot < 0 || slot > 1)
        return NULL;
    struct card* card = &cards[slot];
    if (!card->loaded) {
        card->loaded = 1;
        const char* path = getenv(slot ? "ROOD_CARD2" : "ROOD_CARD1");
        snprintf(card->path, sizeof card->path, "%s",
            path && *path ? path : slot ? "pc/save/card2.mcd" : "pc/save/card1.mcd");
        FILE* f = fopen(card->path, "rb");
        card->present = slot == 0 || f;
        card->format = FORMAT_RAW;
        card->writable = 1;
        if (f) {
            card->format = load_image(card, f);
            fclose(f);
            if (card->format == FORMAT_UNKNOWN) {
                /* Never written: a wrong path must not destroy the file. */
                card->writable = 0;
                format(card);
                fprintf(stderr,
                    "[card] slot %d: %s is not a memory card image this port reads (raw .mcd/.mcr/.srm, "
                    "DexDrive .gme, VGS .mem/.vgs, PSP/Vita .vmp); a blank card that refuses saves is "
                    "inserted instead and the file is left alone\n",
                    slot + 1, card->path);
            } else {
                card->writable = formats[card->format].writable;
                fprintf(stderr, "[card] slot %d: %s (%s%s)\n", slot + 1, card->path, formats[card->format].name,
                    card->writable ? "" : ", read only: saves are refused");
            }
        } else if (card->present)
            format(card);
    }
    return card->present ? card : NULL;
}

/* Saves, renames, erases and formats on a card that is never written. */
static int read_only(struct card* card, const char* what)
{
    if (card->writable)
        return 0;
    fprintf(stderr, "[card] %s refused: %s is read only\n", what, card->path);
    return 1;
}
/* libcard ports are 0x00 (slot 1) and 0x10 (slot 2). */
static struct card* port_card(psx_long port) { return slot_card((port >> 4) & 1); }

static void save(struct card* card)
{
    char temp[600];
    if (!card->writable)
        return;
    char* slash = strrchr(card->path, '/');
    if (slash) {
        *slash = 0;
        pc_mkdir(card->path);
        *slash = '/';
    }
    unsigned header = formats[card->format].header;
    if (card->format == FORMAT_GME)
        for (int block = 0; block < 15; block++) {
            /* The DexDrive header's copy of each directory frame's state and next-block bytes */
            card->header[22 + block] = frame(card, block + 1)[0];
            card->header[38 + block] = frame(card, block + 1)[8];
        }
    snprintf(temp, sizeof temp, "%s.tmp", card->path);
    FILE* f = fopen(temp, "wb");
    if (!f || fwrite(card->header, 1, header, f) != header || fwrite(card->image, 1, CARD_SIZE, f) != CARD_SIZE
        || fclose(f)) {
        fprintf(stderr, "[card] cannot write %s: %s\n", temp, strerror(errno));
        return;
    }
    if (pc_replace_file(temp, card->path))
        fprintf(stderr, "[card] cannot replace %s: %s\n", card->path, strerror(errno));
}

/* "bu00:NAME" -> slot index and NAME; NULL for other devices. */
static const char* parse(const char* path, int* slot)
{
    if (strncmp(path, "bu", 2) || (path[2] != '0' && path[2] != '1') || path[4] != ':')
        return NULL;
    *slot = path[2] - '0';
    return path + 5;
}
static int match(const char* pattern, const char* name)
{
    for (; *pattern; pattern++, name++) {
        if (*pattern == '*')
            return 1;
        if (*pattern != '?' && *pattern != *name)
            return 0;
    }
    return !*name;
}
static int find(struct card* card, const char* name)
{
    for (int block = 0; block < 15; block++) {
        unsigned char* entry = frame(card, block + 1);
        if (get32(entry) == 0x51 && !strncmp((char*)entry + 10, name, 21))
            return block;
    }
    return -1;
}
static int next_block(struct card* card, int block)
{
    unsigned next = get16(frame(card, block + 1) + 8);
    return next < 15 ? (int)next : -1;
}
static void release(struct card* card, int block)
{
    while (block >= 0) {
        int next = next_block(card, block);
        free_entry(card, block);
        block = next;
    }
}
static int create(struct card* card, const char* name, int blocks)
{
    int chosen[15], count = 0;
    if (blocks < 1 || strlen(name) > 20 || find(card, name) >= 0)
        return -1;
    for (int block = 0; block < 15 && count < blocks; block++)
        if ((get32(frame(card, block + 1)) & 0xF0) == 0xA0)
            chosen[count++] = block;
    if (count < blocks)
        return -1;
    for (int i = 0; i < blocks; i++) {
        unsigned char* entry = frame(card, chosen[i] + 1);
        memset(entry, 0, FRAME_SIZE);
        put32(entry, i == 0 ? 0x51 : i == blocks - 1 ? 0x53 : 0x52);
        put16(entry + 8, i == blocks - 1 ? 0xFFFF : chosen[i + 1]);
        if (i == 0) {
            put32(entry + 4, blocks * BLOCK_SIZE);
            strncpy((char*)entry + 10, name, 20);
        }
        seal(entry);
        memset(card->image + (chosen[i] + 1) * BLOCK_SIZE, 0, BLOCK_SIZE);
    }
    return chosen[0];
}

psx_long _card_info(psx_long port)
{
    pc_deliver_event(SW_CARD, port_card(port) ? EV_IOE : EV_TIMEOUT);
    return 1;
}
psx_long _card_load(psx_long port)
{
    struct card* card = port_card(port);
    pc_deliver_event(SW_CARD, card ? unformatted ? EV_NEW : EV_IOE : EV_TIMEOUT);
    return 1;
}
psx_long _card_clear(psx_long port)
{
    pc_deliver_event(HW_CARD, port_card(port) ? EV_IOE : EV_TIMEOUT);
    return 1;
}
psx_long _card_format(psx_long port)
{
    struct card* card = port_card(port);
    if (!card || read_only(card, "format"))
        return 0;
    format(card);
    save(card);
    unformatted = 0;
    return 1;
}

/* The editor's Room Browser can refuse saves while the game is in a room it
 * was warped to (room_warp.c): a save there would resume in that room. The
 * game sees a card that cannot be written. */
int pc_room_warp_blocks_saving(void) __attribute__((weak));
static int saving_blocked(const char* what, const char* path)
{
    if (fail_writes) {
        fprintf(stderr, "[card] %s %s refused: the card RPC's fail_writes\n", what, path);
        return 1;
    }
    if (!pc_room_warp_blocks_saving || !pc_room_warp_blocks_saving())
        return 0;
    fprintf(stderr, "[card] %s %s refused: warped by the Room Browser (Return, or allow saving there)\n", what, path);
    return 1;
}

psx_long pc_psx_open(const char* path, psx_ulong mode)
{
    int slot, block, fd;
    if ((mode & (FILE_WRITE | FILE_CREATE)) && saving_blocked("open for writing", path))
        return -1;
    const char* name = parse(path, &slot);
    struct card* card = name ? slot_card(slot) : NULL;
    if (!card || ((mode & (FILE_WRITE | FILE_CREATE)) && read_only(card, "open for writing")))
        return -1;
    for (fd = 0; fd < 16 && files[fd].used; fd++)
        ;
    if (fd == 16)
        return -1;
    if (mode & FILE_CREATE) {
        if ((block = create(card, name, mode >> 16)) < 0)
            return -1;
        save(card);
    } else if ((block = find(card, name)) < 0)
        return -1;
    files[fd].used = 1;
    files[fd].slot = slot;
    files[fd].first = block;
    files[fd].async = !!(mode & FILE_ASYNC);
    files[fd].position = 0;
    files[fd].size = get32(frame(card, block + 1) + 4);
    return fd;
}
static psx_long transfer(psx_long fd, unsigned char* buffer, psx_long bytes, int writing)
{
    if (fd < 0 || fd >= 16 || !files[fd].used || bytes < 0)
        return -1;
    struct card* card = slot_card(files[fd].slot);
    if (writing && read_only(card, "write"))
        return -1;
    psx_long done = 0;
    while (done < bytes && files[fd].position < files[fd].size) {
        psx_long position = files[fd].position;
        int block = files[fd].first;
        for (psx_long skip = position / BLOCK_SIZE; skip && block >= 0; skip--)
            block = next_block(card, block);
        if (block < 0)
            break;
        psx_long offset = position % BLOCK_SIZE, chunk = BLOCK_SIZE - offset;
        if (chunk > bytes - done)
            chunk = bytes - done;
        unsigned char* data = card->image + (block + 1) * BLOCK_SIZE + offset;
        if (writing)
            memcpy(data, buffer + done, chunk);
        else
            memcpy(buffer + done, data, chunk);
        done += chunk;
        files[fd].position += chunk;
    }
    if (writing && done) {
        ++card_writes;
        save(card);
    }
    return done;
}
/* Asynchronous (FASYNC) transfers complete at once with a SwCARD IOE event. */
static psx_long finish(psx_long fd, psx_long result)
{
    if (result >= 0 && files[fd].async)
        pc_deliver_event(SW_CARD, EV_IOE);
    return result;
}
psx_long pc_psx_read(psx_long fd, void* buffer, psx_long bytes) { return finish(fd, transfer(fd, buffer, bytes, 0)); }
psx_long pc_psx_write(psx_long fd, void* buffer, psx_long bytes) { return finish(fd, transfer(fd, buffer, bytes, 1)); }
psx_long pc_psx_close(psx_long fd)
{
    if (fd < 0 || fd >= 16 || !files[fd].used)
        return -1;
    files[fd].used = 0;
    return fd;
}
/* BIOS rename and erase return 1 on success and 0 on failure. */
psx_long pc_psx_rename(const char* from, const char* to)
{
    int slot, other;
    if (saving_blocked("rename", from))
        return 0;
    const char* old_name = parse(from, &slot);
    const char* new_name = parse(to, &other);
    struct card* card = old_name && new_name && slot == other ? slot_card(slot) : NULL;
    int block = card ? find(card, old_name) : -1;
    if (block < 0 || strlen(new_name) > 20 || find(card, new_name) >= 0 || read_only(card, "rename"))
        return 0;
    unsigned char* entry = frame(card, block + 1);
    memset(entry + 10, 0, 21);
    strncpy((char*)entry + 10, new_name, 20);
    seal(entry);
    save(card);
    return 1;
}
psx_long erase(const char* path)
{
    int slot;
    if (saving_blocked("erase", path))
        return 0;
    const char* name = parse(path, &slot);
    struct card* card = name ? slot_card(slot) : NULL;
    int block = card ? find(card, name) : -1;
    if (block < 0 || read_only(card, "erase"))
        return 0;
    release(card, block);
    save(card);
    return 1;
}

static struct {
    int slot, block;
    char pattern[24];
} listing;
struct DIRENTRY* nextfile(struct DIRENTRY* entry)
{
    struct card* card = slot_card(listing.slot);
    while (card && listing.block < 15) {
        unsigned char* dir = frame(card, ++listing.block);
        if (get32(dir) != 0x51 || !match(listing.pattern, (char*)dir + 10))
            continue;
        memset(entry, 0, sizeof *entry);
        strncpy(entry->name, (char*)dir + 10, sizeof entry->name - 1);
        entry->attr = 0x51;
        entry->size = get32(dir + 4);
        entry->head = listing.block - 1;
        return entry;
    }
    return NULL;
}
struct DIRENTRY* firstfile(const char* path, struct DIRENTRY* entry)
{
    const char* pattern = parse(path, &listing.slot);
    if (!pattern)
        return NULL;
    snprintf(listing.pattern, sizeof listing.pattern, "%s", pattern);
    listing.block = 0;
    return nextfile(entry);
}
