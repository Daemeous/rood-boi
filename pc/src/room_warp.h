/* The game's rooms (read from the disc at run time) and the Room Browser's
 * warp: room_warp.c (battle builds; weak elsewhere), room_browser.cpp. */
#ifndef ROOD_ROOM_WARP_H
#define ROOD_ROOM_WARP_H
#ifdef __cplusplus
extern "C" {
#endif

#define PC_ROOM_ENTRIES 8
typedef struct {
    int location; /* index in the executable's location table (MAPnnn.MPD) */
    int zone, room;
    int scene; /* area (0-31), -1 unknown */
    char name[96]; /* the game's room name (UTF-8), "" when it has none */
    char area[64];
    int entries; /* doors into it (arrival points; 0: reached by an event or a fall) */
    unsigned char door[PC_ROOM_ENTRIES][12]; /* the door records leading here */
    int from[PC_ROOM_ENTRIES]; /* the location each door is in */
    int floor_w, floor_h; /* its floor grid in cells (collision map), 0: unknown */
} pc_room_info;

int pc_rooms_count(void) __attribute__((weak)); /* builds the list once (tens of ms) */
const pc_room_info* pc_rooms_get(int i) __attribute__((weak));
const char* pc_rooms_error(void) __attribute__((weak));
int pc_rooms_json(char* out, int size) __attribute__((weak));

/* Ask for a warp (taken in the field loop); 0 and `why` when refused. */
int pc_room_warp_request(int zone, int room, int entry, char* why, int size) __attribute__((weak));
/* Whether RPC room_goto's arrival cell (when one is given) is on the room's floor. */
int pc_room_warp_cell_valid(int zone, int room, char* why, int size) __attribute__((weak));
void pc_room_warp_cancel(void) __attribute__((weak));
/* 0 idle, 1 requested, 2 loading, 3 arrived (fading in), 4 done (until acknowledged). */
int pc_room_warp_phase(void) __attribute__((weak));
void pc_room_warp_acknowledge(void) __attribute__((weak));
/* Whether the game is in a warped-to room, and where the warps started. */
int pc_room_warp_warped(int* zone, int* room) __attribute__((weak));
void pc_room_warp_clear(void) __attribute__((weak));
const char* pc_room_warp_message(void) __attribute__((weak));
int pc_room_warp_blocks_saving(void) __attribute__((weak));
extern int pc_room_warp_enemies __attribute__((weak)); /* 1: warped-to rooms get their enemies */
extern int pc_room_warp_scripts __attribute__((weak)); /* 1: their scripts run */
extern int pc_room_warp_block_saves __attribute__((weak)); /* refuse card writes while warped */

/* room_browser.cpp */
int pc_room_browser_go(int zone, int room, int entry, char* why, int size);
void pc_room_browser_frame(void);
void pc_room_browser_window(void);

#ifdef __cplusplus
}
#endif
#endif
