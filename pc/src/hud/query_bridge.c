/* Lua queries, the game side (game_query.h; battle builds, compiled with the
 * game headers): what rood.game reads, copied out of the game's records.
 *   field     vs_battle_roomData.unk0 1 and the character state there (as the
 *             HUD bridge's field check)
 *   Ashley    her HUD record (vs_battle_characterState->unk3C, as MENU4's
 *             _renderBasicStats and the Ashley tab read it) and her actor
 *             (D_800F4538[0]: position, facing, weaponDrawn); statuses as the
 *             Status screen lists them (ashley_bridge.c)
 *   room      the live room (D_800F1AB0, else the saved one, as dev_state.c)
 *   flags     first person (_firstPersonViewEnabled), the field menu
 *             (weapon_slots.c), an event script (scene_submit.inc)
 * Pointers are followed only into guest RAM: during room changes and the
 * attract demo the slots can hold stale values. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/3A1A0.h"
#include "../../../src/SLUS_010.40/main.h"
#include "../room_warp.h"
#define PC_GAME_QUERY_DEFINES /* strong definitions here (game_query.h) */
#include "game_query.h"

extern _mpdRoomDoorSection_t D_800F1AB0; /* the live room (dev_state.c) */
extern int _firstPersonViewEnabled;
int pc_battle_field_menu_open(void);                       /* weapon_slots.c */
int pc_battle_event_running(void) __attribute__((weak));   /* scene_submit.inc */
int pc_ui_ashley_statuses(vs_battle_actor2* a, pc_game_status* out, int max); /* ui/ashley_bridge.c */

static int guest(const void* p, unsigned size)
{
    __UINTPTR_TYPE__ a = (__UINTPTR_TYPE__)p;
    return a >= 0x80010000ul && a + size <= 0x80200000ul && !(a & 3);
}

void pc_game_query_fill(pc_game_query* out)
{
    const _mpdRoomDoorSection_t* location;
    vs_battle_actor2* a = NULL;
    D_800F4538_t* actor;
    int i;
    for (i = 0; i < (int)sizeof *out; ++i)
        ((char*)out)[i] = 0;
    out->zone = out->room = -1;
    out->field = vs_battle_roomData.unk0 == 1 && vs_battle_characterState != NULL;
    if (!out->field)
        return;
    location = D_800F1AB0.zndId ? &D_800F1AB0 : &D_80061068;
    out->zone = location->zndId;
    out->room = location->mpdId;
    out->first_person = _firstPersonViewEnabled != 0;
    out->menu = pc_battle_field_menu_open();
    out->event = pc_battle_event_running && pc_battle_event_running();
    if (guest(vs_battle_characterState, sizeof *vs_battle_characterState))
        a = psx_get(vs_battle_characterState->unk3C);
    actor = D_800F4538[0];
    if (!a || !guest(a, sizeof *a) || !guest(actor, sizeof *actor))
        return;
    out->ashley = 1;
    out->hp = a->currentHP;
    out->max_hp = a->maxHP;
    out->mp = a->currentMP;
    out->max_mp = a->maxMP;
    out->risk = a->risk;
    out->str = a->strength + a->accessory.currentStr;
    out->intel = a->intelligence + a->accessory.currentInt;
    out->agl = a->agility + a->accessory.currentAgility;
    out->x = actor->unk0.position.vx;
    out->y = actor->unk0.position.vy;
    out->z = actor->unk0.position.vz;
    out->facing = actor->unk0.facing & 0xFFF;
    out->battle = actor->unk0.weaponDrawn;
    out->status_count = pc_ui_ashley_statuses(a, out->statuses, PC_GAME_STATUSES);
}

int pc_game_query_room_names(int zone, int room, char* area, int area_size, char* name, int name_size)
{
    int i, n, k;
    if (!pc_rooms_count || !pc_rooms_get || (n = pc_rooms_count()) <= 0)
        return 0;
    for (i = 0; i < n; ++i) {
        const pc_room_info* r = pc_rooms_get(i);
        if (!r || r->zone != zone || r->room != room)
            continue;
        for (k = 0; k + 1 < area_size && r->area[k]; ++k)
            area[k] = r->area[k];
        area[k] = 0;
        for (k = 0; k + 1 < name_size && r->name[k]; ++k)
            name[k] = r->name[k];
        name[k] = 0;
        return 1;
    }
    return 0;
}
