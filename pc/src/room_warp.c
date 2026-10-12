/* The Room Browser's warp, game side (host side and design: room_catalog.c).
 * Compiled with the game headers; the src hooks (ROOD_PC) call these. */
#include "common.h"
#include "../../src/BATTLE/BATTLE.PRG/146C.h"

extern char D_800F4C34; /* an event script is running */
extern short D_800F4BE2;
extern short _mpdScriptSection; /* 1: the room's entry script runs on entering */
extern void* _mpdClearedSection; /* the room's trigger table (u16 offsets, 0xFFFF ends it) */
extern _mpdRoomDoorSection_t D_800F1AB0; /* where the player is (the last door's destination) */
int func_8007C4E0(_mpdRoomDoorSection_t* door, int effect, int speed);
int func_8007C5C0(_mpdRoomDoorSection_t* door, int effect, int speed);
void func_800A0ABC(int actor);

int pc_room_warp_pending(void);
int pc_room_warp_take(int zone, int room, unsigned char door[12]);
int pc_room_warp_arrived(void);
int pc_room_warp_finish(int zone, int room);
extern int pc_room_warp_enemies, pc_room_warp_scripts;

static _mpdRoomDoorSection_t door; /* what func_8007C4E0 / func_8007C5C0 copy */
static vs_main_mapStatus_t map_before;

/* RPC ending (developer jump): play the ending from the field, as the final
 * event script's special op 1 does (vs_battle_script_specialOp). It loads
 * ENDING.PRG over BATTLE and returns to the title, so it does not return. */
volatile int pc_ending_request;

/* vs_battle_exec, the field (case 1), where doors are checked: start a
 * requested warp as a door does (1: the loop breaks out, as after a door). */
int pc_room_warp_poll(void)
{
    if (pc_ending_request && vs_battle_screenTransitionStep == 0 && D_800F4C34 == 0 && D_800F4BE2 == 0
        && !vs_main_stateFlags.gameOver) {
        pc_ending_request = 0;
        vs_main_showEndingAndReturnToTitle();
    }
    if (pc_room_warp_finish(D_800F1AB0.zndId, D_800F1AB0.mpdId)) {
        /* The first field frame there: the map screen keeps no trace of it. */
        vs_main_mapStatus = map_before;
        return 0;
    }
    if (!pc_room_warp_pending())
        return 0;
    if (vs_battle_screenTransitionStep != 0 || D_800F4C34 != 0 || D_800F4BE2 != 0 || vs_main_stateFlags.gameOver)
        return 0;
    if (!pc_room_warp_take(D_800F1AB0.zndId, D_800F1AB0.mpdId, (unsigned char*)&door))
        return 0;
    map_before = vs_main_mapStatus;
    if (door.zndId == _zoneContext.zndId)
        func_8007C4E0(&door, 0, 8);
    else
        func_8007C5C0(&door, 0, 8); /* another zone: its file loads first */
    func_800A0ABC(0);
    return 1;
}

/* func_80089DC0: the room's sections are loaded; its enemies spawn and its
 * entry trigger runs next. */
void pc_room_warp_room_loaded(void)
{
    if (!pc_room_warp_arrived())
        return;
    if (!pc_room_warp_enemies)
        _zoneContext.mpdEnemyCount = 0;
    if (!pc_room_warp_scripts) {
        _mpdScriptSection = 0; /* no entry script (cutscene) */
        if (_mpdClearedSection)
            *(u_short*)_mpdClearedSection = 0xFFFF; /* no triggers this visit */
    }
}
