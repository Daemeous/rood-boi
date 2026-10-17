/* Room browser: every zone and room of the game, and a warp that makes the
 * game itself load one (the editor's Room Browser, RPC rooms / room_goto).
 *
 * The list is built at run time from the user's disc (nothing is shipped):
 * the executable's location table (vs_main_locationZoneMapIds: location ->
 * zone, room), the zone files (ZONEnnn.ZND: the room files' sectors), each
 * room's door table (MAPnnn.MPD: where a door leads and where Ashley
 * arrives), the area names (MENU/MENU5.PRG) and the room names
 * (SMALL/SCENnnn.ARM, as the game's map screen reads them).
 *
 * The warp is the game's own door transition (room_warp.c, the game side): a request is taken in the
 * field loop (vs_battle_exec, case 1, where func_80074BAC handles doors;
 * src hook pc_room_warp_poll under ROOD_PC) by calling func_8007C4E0 (same
 * zone) or func_8007C5C0 (another zone: the zone file is loaded) with a
 * door record whose destination and arrival cell come from a real door
 * into the target room. While the new room loads (func_80089DC0, src hook
 * pc_room_warp_room_loaded, pc_room_warp_room_spawned) the warp can bench
 * the room's enemies (spawned, so its scripts find them, but hidden and out
 * of the fight whenever no event runs) and leave its scripts unrun (no
 * cutscene, no story flag set by entering; door scripts stay), and the
 * map's "room seen" bits are put back afterwards. Saving can be refused
 * while warped (memory_card.c asks pc_room_warp_blocks_saving). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The executable's own tables (game data, linked from the user's build). */
extern char vs_main_locationZoneMapIds[] __attribute__((weak)); /* 512 x (zone, room) */
extern struct {
    int lba, size;
} vs_main_zndFiles[] __attribute__((weak)); /* by zone: ZONEnnn.ZND */

#include "room_warp.h"
#define PC_ROOMS_MAX 512

static pc_room_info rooms[PC_ROOMS_MAX];
static int room_count = -1;
static char catalog_error[160];
static double catalog_ms;

/* ---- the disc ---- */
static FILE* disc;
static int read_sector(int lba, unsigned char* out)
{
    unsigned char sector[2352];
    if (fseek(disc, (long)lba * 2352, SEEK_SET) || fread(sector, 1, sizeof sector, disc) != sizeof sector
        || (sector[15] != 1 && sector[15] != 2))
        return 0;
    memcpy(out, sector + (sector[15] == 2 ? 24 : 16), 2048);
    return 1;
}
static unsigned le32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24; }
static unsigned le16(const unsigned char* p) { return p[0] | p[1] << 8; }
/* `size` bytes at byte `offset` of the file starting at `lba` (malloc). */
static unsigned char* read_bytes(int lba, unsigned offset, unsigned size)
{
    unsigned char* out = malloc(size ? size : 1);
    unsigned char block[2048];
    unsigned done = 0;
    while (out && done < size) {
        unsigned at = offset + done;
        if (!read_sector(lba + (int)(at / 2048), block)) {
            free(out);
            return NULL;
        }
        unsigned n = 2048 - at % 2048;
        if (n > size - done)
            n = size - done;
        memcpy(out + done, block + at % 2048, n);
        done += n;
    }
    return out;
}
/* ISO 9660: the file's first LBA and size, -1 when missing. */
static int find_file(const char* path, unsigned* size_out)
{
    unsigned char block[2048];
    if (!read_sector(16, block) || block[0] != 1 || memcmp(block + 1, "CD001", 5))
        return -1;
    unsigned lba = le32(block + 156 + 2), size = le32(block + 156 + 10);
    for (;;) {
        const char* slash = strchr(path, '/');
        size_t len = slash ? (size_t)(slash - path) : strlen(path);
        int found = 0;
        for (unsigned offset = 0; offset < size && !found; offset += 2048) {
            if (!read_sector((int)(lba + offset / 2048), block))
                return -1;
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
            return -1;
        if (!slash) {
            if (size_out)
                *size_out = size;
            return (int)lba;
        }
        path = slash + 1;
    }
}
static unsigned char* read_file(const char* path, unsigned* size)
{
    int lba = find_file(path, size);
    return lba < 0 ? NULL : read_bytes(lba, 0, *size);
}

/* ---- the game's text (tools/etc/vsString.py has the full table) ---- */
static const char* const glyphs[0xB2] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
    "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "OE", "\xc3\x80", "\xc3\x81",
    "\xc3\x82", "\xc3\x84", "\xc3\x87", "\xc3\x88", "\xc3\x89", "\xc3\x8a", "\xc3\x8b", "\xc3\x8c", "\xc3\x8d",
    "\xc3\x8e", "\xc3\x8f", "\xc3\x92", "\xc3\x93", "\xc3\x94", "\xc3\x96", "\xc3\x99", "\xc3\x9a", "\xc3\x9b",
    "\xc3\x9c", "\xc3\x9f", "oe", "\xc3\xa0", "\xc3\xa1", "\xc3\xa2", "\xc3\xa4", "\xc3\xa7", "\xc3\xa8", "\xc3\xa9",
    "\xc3\xaa", "\xc3\xab", "\xc3\xac", "\xc3\xad", "\xc3\xae", "\xc3\xaf", "\xc3\xb2", "\xc3\xb3", "\xc3\xb4",
    "\xc3\xb6", "\xc3\xb9", "\xc3\xba", "\xc3\xbb", "\xc3\xbc", [0x8F] = " ", [0x90] = "!",
    [0x91] = "\"", [0x92] = "#", [0x93] = "$", [0x94] = "%", [0x95] = "&", [0x96] = "'", [0x97] = "(", [0x98] = ")",
    [0x99] = "=", [0x9A] = "@", [0x9B] = "[", [0x9C] = "]", [0x9D] = ";", [0x9E] = ":", [0x9F] = ",", [0xA0] = ".",
    [0xA1] = "/", [0xA2] = "\\", [0xA3] = "<", [0xA4] = ">", [0xA5] = "?", [0xA6] = "_", [0xA7] = "-", [0xA8] = "+",
    [0xA9] = "*", [0xAA] = "`", [0xAB] = "{", [0xAC] = "}", [0x8C] = "\xc2\xb7", [0x8D] = "-", [0x8E] = "...",
};
static void decode(const unsigned char* s, unsigned max, char* out, size_t size)
{
    size_t used = 0;
    out[0] = 0;
    for (unsigned i = 0; i < max; i++) {
        unsigned c = s[i];
        if (c == 0xE7)
            break;
        if (c >= 0xF8) { /* a control code and its argument */
            ++i;
            continue;
        }
        if (c == 0xE8 && used) {
            c = 0x8F; /* a line break: a space */
        }
        const char* g = c < sizeof glyphs / sizeof glyphs[0] ? glyphs[c] : NULL;
        if (!g)
            continue;
        size_t n = strlen(g);
        if (used + n + 1 >= size)
            break;
        memcpy(out + used, g, n + 1);
        used += n;
    }
    while (used && out[used - 1] == ' ')
        out[--used] = 0;
}

/* ---- the catalog ---- */
static pc_room_info* room_at(int zone, int room)
{
    for (int i = 0; i < room_count; i++)
        if (rooms[i].zone == zone && rooms[i].room == room)
            return &rooms[i];
    return NULL;
}
static int by_zone_room(const void* a, const void* b)
{
    const pc_room_info *x = a, *y = b;
    return x->zone != y->zone ? x->zone - y->zone : x->room - y->room;
}
static int build_catalog(void)
{
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    room_count = 0;
    if (!vs_main_locationZoneMapIds || !vs_main_zndFiles) {
        snprintf(catalog_error, sizeof catalog_error, "the game's location tables are not in this build");
        return 0;
    }
    const char* image = getenv("ROOD_DISC");
    disc = fopen(image && *image ? image : "disks/SLUS-01040.bin", "rb");
    if (!disc) {
        snprintf(catalog_error, sizeof catalog_error, "cannot open the disc image");
        return 0;
    }
    /* Locations (zone, room) from the executable's table; zone 0 is the
     * debug zone and location 0 (100, 0) a placeholder. */
    int zone_of[PC_ROOMS_MAX], room_of[PC_ROOMS_MAX];
    for (int n = 0; n < PC_ROOMS_MAX; n++) {
        zone_of[n] = (unsigned char)vs_main_locationZoneMapIds[n * 2];
        room_of[n] = (unsigned char)vs_main_locationZoneMapIds[n * 2 + 1];
        if (!n || !zone_of[n] || zone_of[n] >= 100 || room_at(zone_of[n], room_of[n]))
            continue;
        pc_room_info* r = &rooms[room_count++];
        memset(r, 0, sizeof *r);
        r->location = n, r->zone = zone_of[n], r->room = room_of[n], r->scene = -1;
    }
    /* Each zone's room files, and each room's doors: arrival points into
     * the rooms they lead to. */
    for (int z = 1; z < 100; z++) {
        int lba = vs_main_zndFiles[z].lba, size = vs_main_zndFiles[z].size;
        if (lba <= 0 || size < 0x1C)
            continue;
        unsigned char* header = read_bytes(lba, 0, 0x1C);
        if (!header)
            continue;
        unsigned mpd_offset = le32(header), mpd_len = le32(header + 4);
        free(header);
        if (mpd_len > 8 * 64 || mpd_offset + mpd_len > (unsigned)size)
            continue;
        unsigned char* list = read_bytes(lba, mpd_offset, mpd_len);
        for (unsigned k = 0; list && k < mpd_len / 8; k++) {
            int mpd = (int)le32(list + 8 * k);
            unsigned mpd_size = le32(list + 8 * k + 4);
            pc_room_info* self = room_at(z, (int)k);
            if (mpd <= 0 || mpd_size < 0x30)
                continue;
            unsigned char* h = read_bytes(mpd, 0, 0x30);
            if (!h)
                continue;
            unsigned room_offset = le32(h), room_len = le16(h + 4);
            free(h);
            if (!room_len || room_offset + 0x60 > mpd_size)
                continue;
            unsigned char* lengths = read_bytes(mpd, room_offset, 0x60);
            if (!lengths)
                continue;
            unsigned at = room_offset + 0x60, door_at = 0, door_len = 0, scene_at = 0, floor_at = 0, objects_at = 0,
                     objects_len = 0;
            for (int s = 0; s < 24; s++) {
                unsigned len = le32(lengths + 4 * s);
                if (s == 1 && len >= 4)
                    floor_at = at; /* the collision map: width, height (cells) */
                if (s == 3)
                    door_at = at, door_len = len;
                if (s == 8)
                    objects_at = at, objects_len = len; /* section 9: 12-byte objects */
                if (s == 10 && len >= 0x52)
                    scene_at = at + 0x50;
                at += len;
            }
            free(lengths);
            if (self && scene_at && scene_at + 2 <= mpd_size) {
                unsigned char* scene = read_bytes(mpd, scene_at, 2);
                if (scene) {
                    int id = (short)le16(scene);
                    self->scene = id >= 0 && id < 32 ? id : -1;
                    free(scene);
                }
            }
            if (self && floor_at && floor_at + 4 <= mpd_size) {
                unsigned char* grid = read_bytes(mpd, floor_at, 4);
                if (grid) {
                    self->floor_w = (short)le16(grid);
                    self->floor_h = (short)le16(grid + 2);
                    free(grid);
                }
            }
            if (self && objects_len >= 12 && objects_len <= 12 * 256 && objects_at + objects_len <= mpd_size) {
                /* _mpdRoomSection9: x, z, level, type (16: a Circle of Magic), lock, id (unkA) */
                unsigned char* objects = read_bytes(mpd, objects_at, objects_len);
                for (unsigned o = 0; objects && o < objects_len / 12 && !self->circle_id; o++) {
                    const unsigned char* e = objects + 12 * o;
                    if (le16(e + 6) == 16) {
                        self->circle_id = e[10];
                        self->circle_cell[0] = (short)le16(e);
                        self->circle_cell[1] = (short)le16(e + 2);
                        self->circle_cell[2] = (short)le16(e + 4);
                    }
                }
                free(objects);
            }
            if (!door_len || door_len > 12 * 64 || door_at + door_len > mpd_size)
                continue;
            unsigned char* doors = read_bytes(mpd, door_at, door_len);
            for (unsigned d = 0; doors && d < door_len / 12; d++) {
                const unsigned char* e = doors + 12 * d;
                pc_room_info* to = room_at(e[0], e[1]);
                if (!to || to->entries >= PC_ROOM_ENTRIES || (le16(e + 2) >> 13) == 7) /* 7: a pit fall */
                    continue;
                memcpy(to->door[to->entries], e, 12);
                to->from[to->entries] = self ? self->location : -1;
                to->entries++;
            }
            free(doors);
        }
        free(list);
    }
    /* Names: the area of each scene (MENU5.PRG) and the rooms of each scene
     * (SCENnnn.ARM: count, {visited, size, zone, room + 1}, blobs, names). */
    unsigned size = 0;
    unsigned char* menu = read_file("MENU/MENU5.PRG", &size);
    char areas[32][64];
    memset(areas, 0, sizeof areas);
    if (menu && size > 0x5BFC + 64) {
        const unsigned char* table = menu + 0x5BFC;
        for (int s = 0; s < 32; s++) {
            unsigned index = le16(table + 2 * s);
            if (0x5BFC + 2 * index < size)
                decode(table + 2 * index, size - (0x5BFC + 2 * index), areas[s], sizeof areas[s]);
        }
    }
    free(menu);
    for (int s = 1; s < 31; s++) { /* scene 31's file has no names */
        char path[32];
        snprintf(path, sizeof path, "SMALL/SCEN%03d.ARM", s);
        unsigned char* arm = read_file(path, &size);
        if (!arm || size < 4) {
            free(arm);
            continue;
        }
        unsigned count = le32(arm), blobs = 0;
        if (count > 256 || 4 + 12 * count > size) {
            free(arm);
            continue;
        }
        for (unsigned i = 0; i < count; i++)
            blobs += le32(arm + 4 + 12 * i + 4);
        unsigned names = 4 + 12 * count + blobs;
        if (names + 36 * count > size)
            names = size - 36 * count;
        for (unsigned i = 0; i < count; i++) {
            int z = (short)le16(arm + 4 + 12 * i + 8), m = (short)le16(arm + 4 + 12 * i + 10) - 1;
            pc_room_info* r = room_at(z, m);
            if (!r)
                continue;
            decode(arm + names + 36 * i, 32, r->name, sizeof r->name);
            r->scene = s;
        }
        free(arm);
    }
    for (int i = 0; i < room_count; i++)
        if (rooms[i].scene >= 0)
            snprintf(rooms[i].area, sizeof rooms[i].area, "%s", areas[rooms[i].scene]);
    /* By zone, then room: the browser's order (Previous / Next). */
    qsort(rooms, (size_t)room_count, sizeof rooms[0], by_zone_room);
    fclose(disc);
    disc = NULL;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    catalog_ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) * 1e-6;
    fprintf(stderr, "[rooms] %d rooms from the disc in %.0f ms\n", room_count, catalog_ms);
    return room_count;
}

int pc_rooms_count(void)
{
    if (room_count < 0)
        build_catalog();
    return room_count;
}
const pc_room_info* pc_rooms_get(int i)
{
    return i >= 0 && i < pc_rooms_count() ? &rooms[i] : NULL;
}
const char* pc_rooms_error(void) { return catalog_error; }

/* ---- the warp (room_warp.c is the game side) ---- */
enum { WARP_IDLE, WARP_PENDING, WARP_LOADING, WARP_ARRIVED, WARP_DONE };
static int phase;
static int target; /* rooms[] index */
static int target_entry;
static int warped; /* the game is in a room it was warped to */
static int origin_zone = -1, origin_room = -1;
static int now_zone = -1, now_room = -1;
int pc_room_warp_enemies = 0; /* 1: the room's enemies appear */
int pc_room_warp_scripts = 0; /* 1: the room's scripts run (cutscenes may set story flags) */
int pc_room_warp_block_saves = 1; /* refuse to write the memory card while warped */
static char warp_message[200];
/* RPC room_goto {cell: [x, y, z, facing]}: arrive on this floor cell through
 * a made-up door (rooms without a door into them: cutscene rooms, rooms
 * reached by a fall); x < 0: a real door. Cleared by each request taken. */
int pc_room_warp_cell[4] = { -1, 0, 0, 0 };

/* Whether pc_room_warp_cell (when given) is on the room's floor grid: the
 * arrival looks its cell up (func_80074B14 -> func_8008B764), which is NULL
 * off the grid (x >= width or z >= height) and for levels 2 and up
 * (platforms: none to stand on here). 0 and `why` when it is not. */
int pc_room_warp_cell_valid(int zone, int room, char* why, int size)
{
    pc_rooms_count();
    const pc_room_info* r = room_at(zone, room);
    if (!r || pc_room_warp_cell[0] < 0)
        return 1;
    int x = pc_room_warp_cell[0], y = pc_room_warp_cell[1], z = pc_room_warp_cell[2];
    if (r->floor_w <= 0 || r->floor_h <= 0) {
        snprintf(why, size, "zone %d room %d: its floor grid could not be read (no cell warps)", zone, room);
        return 0;
    }
    if (x >= r->floor_w || z < 0 || z >= r->floor_h || y < 0 || y > 1) {
        snprintf(why, size, "cell [%d, %d, %d] is outside zone %d room %d's floor (x 0-%d, y 0-1, z 0-%d)", x, y, z,
            zone, room, r->floor_w - 1, r->floor_h - 1);
        return 0;
    }
    return 1;
}

/* Request a warp (the field loop takes it); 0 when refused (`why`). */
int pc_room_warp_request(int zone, int room, int entry, char* why, int size)
{
    pc_rooms_count();
    pc_room_info* r = room_at(zone, room);
    if (!r) {
        snprintf(why, size, "no zone %d room %d on this disc", zone, room);
        return 0;
    }
    if (!r->entries && pc_room_warp_cell[0] < 0) {
        snprintf(why, size, "zone %d room %d has no door into it (reached by an event or a fall; give a cell)", zone, room);
        return 0;
    }
    if (phase == WARP_LOADING || phase == WARP_ARRIVED) {
        snprintf(why, size, "a warp is under way");
        return 0;
    }
    if (!pc_room_warp_cell_valid(zone, room, why, size)) {
        pc_room_warp_cell[0] = -1;
        return 0;
    }
    target = (int)(r - rooms);
    target_entry = entry >= 0 && entry < r->entries ? entry : 0;
    phase = WARP_PENDING;
    snprintf(warp_message, sizeof warp_message, "Warp to zone %d room %d requested (waiting for the field)", zone, room);
    return 1;
}
void pc_room_warp_cancel(void)
{
    if (phase == WARP_PENDING)
        phase = WARP_IDLE;
}
int pc_room_warp_pending(void) { return phase == WARP_PENDING; }

/* The game side takes the request in the field loop: the door record to
 * use (12 bytes, the game's _mpdRoomDoorSection_t). */
int pc_room_warp_take(int zone, int room, unsigned char door[12])
{
    if (phase != WARP_PENDING)
        return 0;
    const pc_room_info* r = &rooms[target];
    char why[160];
    if (!pc_room_warp_cell_valid(r->zone, r->room, why, sizeof why)) {
        /* (set after the request was checked) */
        snprintf(warp_message, sizeof warp_message, "Warp refused: %s", why);
        fprintf(stderr, "[warp] %s\n", warp_message);
        pc_room_warp_cell[0] = -1;
        phase = WARP_IDLE;
        return 0;
    }
    if (!warped)
        origin_zone = zone, origin_room = room;
    if (pc_room_warp_cell[0] >= 0) { /* a made-up door onto the cell (as the game's pit falls make one) */
        int x = pc_room_warp_cell[0] & 31, y = pc_room_warp_cell[1] & 7, z = pc_room_warp_cell[2] & 31;
        int facing = pc_room_warp_cell[3] & 3;
        unsigned u2 = (unsigned)(x | z << 5 | y << 10), u4 = (unsigned)(x << 8 | facing << 13), u6 = (unsigned)(z << 10);
        memset(door, 0, 12);
        door[2] = (unsigned char)u2, door[3] = (unsigned char)(u2 >> 8);
        door[4] = (unsigned char)u4, door[5] = (unsigned char)(u4 >> 8);
        door[6] = (unsigned char)u6, door[7] = (unsigned char)(u6 >> 8);
        pc_room_warp_cell[0] = -1;
    } else
        memcpy(door, r->door[target_entry], 12);
    door[0] = (unsigned char)r->zone;
    door[1] = (unsigned char)r->room;
    /* Going back where the warps started ends the warped state. */
    warped = !(r->zone == origin_zone && r->room == origin_room);
    phase = WARP_LOADING;
    fprintf(stderr, "[warp] zone %d room %d -> zone %d room %d (door %d, from location %d)%s%s\n", zone, room, r->zone,
        r->room, target_entry, r->from[target_entry], pc_room_warp_enemies ? "" : ", enemies benched",
        pc_room_warp_scripts ? "" : ", no room scripts");
    return 1;
}
/* The new room's sections are loaded (before its enemies and scripts). */
int pc_room_warp_arrived(void)
{
    if (phase != WARP_LOADING)
        return 0;
    phase = WARP_ARRIVED;
    return 1;
}
/* The first field frame after the warp; 1 once. */
int pc_room_warp_finish(int zone, int room)
{
    now_zone = zone, now_room = room;
    if (phase != WARP_ARRIVED && phase != WARP_LOADING)
        return 0;
    phase = WARP_DONE;
    snprintf(warp_message, sizeof warp_message, "Arrived in zone %d room %d", zone, room);
    fprintf(stderr, "[warp] %s\n", warp_message);
    return 1;
}

/* The game side benched an enemy (enemies off: room_warp.c). */
void pc_room_warp_benched(int slot, int script_id)
{
    fprintf(stderr, "[warp] enemy in slot %d (script id %d) benched\n", slot, script_id);
}

/* 0 idle, 1 requested, 2 loading, 3 arrived, 4 done (until taken). */
int pc_room_warp_phase(void) { return phase; }
void pc_room_warp_acknowledge(void)
{
    if (phase == WARP_DONE)
        phase = WARP_IDLE;
}
int pc_room_warp_warped(int* zone, int* room)
{
    if (zone)
        *zone = origin_zone;
    if (room)
        *room = origin_room;
    return warped;
}
/* The player keeps this room (saving allowed again). */
void pc_room_warp_clear(void) { warped = 0; }

/* savePointStates when a chain of warps started (room_warp.c takes and puts
 * back the game's bytes). Kept here, beside `warped`, so the two share one
 * lifetime: host state, out of save states, and both dropped by
 * pc_room_warp_reset when the game's state is replaced. */
static unsigned char points_before[PC_ROOM_WARP_POINTS];
static int points_held;

void pc_room_warp_points_hold(const unsigned char* states)
{
    if (points_held)
        return;
    memcpy(points_before, states, sizeof points_before);
    points_held = 1;
}

int pc_room_warp_points_release(unsigned char* states)
{
    if (!points_held || warped || (phase >= WARP_PENDING && phase <= WARP_ARRIVED))
        return 0;
    memcpy(states, points_before, sizeof points_before);
    points_held = 0;
    fprintf(stderr, "[warp] the chain of warps ended: savePointStates as before it\n");
    return 1;
}

int pc_room_warp_points_held(void) { return points_held; }

/* The game's state is replaced (boot_sdk.c: the jump to the title, a soft
 * reset, game over, the ending, and the jump back into BATTLE after a load):
 * a chain of warps from the old game ends with it and its snapshot is
 * dropped (put back into the loaded game it would overwrite that save's
 * Circles). */
void pc_room_warp_reset(void)
{
    if (warped || points_held || phase != WARP_IDLE)
        fprintf(stderr, "[warp] the game's state was replaced: the warp state%s dropped\n",
            points_held ? " and its savePointStates snapshot" : "");
    warped = 0;
    origin_zone = origin_room = -1;
    points_held = 0;
    phase = WARP_IDLE;
    pc_room_warp_cell[0] = -1;
}
const char* pc_room_warp_message(void) { return warp_message; }
int pc_room_warp_blocks_saving(void) { return warped && pc_room_warp_block_saves; }

/* RPC rooms: the catalog as JSON. */
static void json_text(char* out, size_t size, size_t* used, const char* s)
{
    if (*used + 2 >= size)
        return;
    out[(*used)++] = '"';
    for (; *s && *used + 8 < size; s++) {
        if (*s == '"' || *s == '\\')
            out[(*used)++] = '\\';
        out[(*used)++] = *s;
    }
    out[(*used)++] = '"';
    out[*used] = 0;
}
int pc_rooms_json(char* out, int size)
{
    size_t used = 0, cap = (size_t)size;
    int n = pc_rooms_count();
    used += (size_t)snprintf(out, cap, "{\"count\":%d,\"ms\":%.1f,\"error\":", n, catalog_ms);
    json_text(out, cap, &used, catalog_error);
    used += (size_t)snprintf(out + used, cap - used, ",\"current\":[%d,%d],\"warped\":%s,\"phase\":%d,\"rooms\":[",
        now_zone, now_room, warped ? "true" : "false", phase);
    for (int i = 0; i < n && used + 300 < cap; i++) {
        const pc_room_info* r = &rooms[i];
        used += (size_t)snprintf(out + used, cap - used, "%s{\"zone\":%d,\"room\":%d,\"location\":%d,\"scene\":%d,\"entries\":%d,"
            "\"floor\":[%d,%d],\"name\":",
            i ? "," : "", r->zone, r->room, r->location, r->scene, r->entries, r->floor_w, r->floor_h);
        json_text(out, cap, &used, r->name);
        used += (size_t)snprintf(out + used, cap - used, ",\"area\":");
        json_text(out, cap, &used, r->area);
        used += (size_t)snprintf(out + used, cap - used, "}");
    }
    used += (size_t)snprintf(out + used, cap - used, "]}");
    return (int)used;
}
