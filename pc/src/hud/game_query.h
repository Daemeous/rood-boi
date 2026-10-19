/* Read-only game state for Lua queries (rood.game, lua_api_game.cpp; plan
 * pc/docs/plans/2026-10-18-lua-visible-layer.md "Queries"). query_bridge.c
 * (battle builds, the game's headers) copies what a query needs into this
 * plain record; lua_api_game.cpp fills it at most once per game vsync and
 * gives each plugin one read-only table per vsync. Copies, never pointers
 * into the game. Every call is on the game thread. */
#ifndef ROOD_GAME_QUERY_H
#define ROOD_GAME_QUERY_H
#ifdef __cplusplus
extern "C" {
#endif

#define PC_GAME_STATUSES 24

typedef struct {
    char name[32]; /* the game's own word (MENU4's status strings: "Poison", "STR Up", ...) */
    int buff;      /* 1 a benefit (MENU4's buff palette), 0 an ailment */
    int seconds;   /* its timer (the game's units, as the Status screen shows it); -1 none */
} pc_game_status;

typedef struct {
    int field;       /* a room is loaded and the battle overlay's world runs */
    int ashley;      /* Ashley's records are there (the fields below are hers) */
    int hp, max_hp, mp, max_mp, risk;
    int str, intel, agl; /* as the Status screen's EQP column: the record's value plus the accessory's */
    int x, y, z;     /* her feet, world units (y down) */
    int facing;      /* 0..4095 */
    int battle;      /* her weapon drawn (battle mode) */
    int status_count;
    pc_game_status statuses[PC_GAME_STATUSES];
    int zone, room;  /* the live room (zone and map numbers), -1 none */
    int first_person;
    int menu;        /* the game's field menu (the ring menu, its screens) is open */
    int event;       /* an event script (a cutscene) runs */
} pc_game_query;

/* query_bridge.c (battle builds; absent elsewhere: weak): the state now. */
void pc_game_query_fill(pc_game_query* out) __attribute__((weak));
/* The room's names (the game's, through room_warp.h's list; built once, tens of
 * ms the first time): 1 when found. */
int pc_game_query_room_names(int zone, int room, char* area, int area_size, char* name, int name_size)
    __attribute__((weak));

#ifdef __cplusplus
}
#endif
#endif
