/* Vagrant Story snapshot for the developer RPC `state` method (dev_rpc.c).
 * Compiled with the game headers so field offsets follow the decompilation. */
#include "common.h"
#include "../../src/BATTLE/BATTLE.PRG/3A1A0.h"
#include "../../src/SLUS_010.40/main.h"

extern int vs_main_startState;
/* 146C.h: the room the player is in (the last door's destination). INITBTL
 * starts it from D_80061068, which then holds the saved location only: the
 * game copies the current room back into it when saving. */
extern _mpdRoomDoorSection_t D_800F1AB0;
/* 146C.h: vs_battle_actors[0] is Ashley; its unk3C points at the HUD record
 * (name[24], then currentHP, maxHP, currentMP, maxMP, risk as shorts). */
extern unsigned char* vs_battle_actors[];
extern int sprintf(char*, const char*, ...);

#include "dev_state.h"
/* Actor records live in the guest heap; during the attract demo and room
 * transitions the slots can hold stale values, so only follow pointers that
 * lie in guest RAM (this runs every frame for the lights). */
static int guest_pointer(const void* p, unsigned size)
{
    __UINTPTR_TYPE__ a = (__UINTPTR_TYPE__)p;
    return a >= 0x80010000ul && a + size <= 0x80200000ul && !(a & 3);
}
void pc_dev_state(struct pc_dev_state* out)
{
    D_800F4538_t* player = D_800F4538[0];
    unsigned char* actor = vs_battle_actors[0];
    short* stats = NULL;
    if (!guest_pointer(player, sizeof *player))
        player = NULL;
    if (guest_pointer(actor, 0x40) && guest_pointer(psx_load(unsigned char*, actor + 0x3c), 0x24))
        stats = (short*)(psx_load(unsigned char*, actor + 0x3c) + 0x18);
    out->valid = 1;
    const _mpdRoomDoorSection_t* location = D_800F1AB0.zndId ? &D_800F1AB0 : &D_80061068;
    out->zone = location->zndId;
    out->room = location->mpdId;
    out->start_state = vs_main_startState;
    out->music_id = vs_main_getCurrentMusicId();
    out->music_slot = vs_main_soundData.currentMusicSlot;
    out->hp = stats ? stats[0] : 0;
    out->max_hp = stats ? stats[1] : 0;
    out->mp = stats ? stats[2] : 0;
    out->max_mp = stats ? stats[3] : 0;
    out->risk = stats ? (unsigned short)stats[4] : 0;
    out->game_over = vs_main_stateFlags.gameOver;
    out->intro_state = vs_main_stateFlags.introState;
    out->has_player = player != NULL;
    if (player != NULL) {
        out->x = player->unk0.position.vx;
        out->y = player->unk0.position.vy;
        out->z = player->unk0.position.vz;
        out->facing = player->unk0.facing;
    }
}
void pc_rpc_state(char* out, unsigned size)
{
    struct pc_dev_state s = { 0 };
    char position[96] = "null";
    (void)size; /* the reply buffer is far larger than this fixed-size object */
    pc_dev_state(&s);
    if (s.has_player)
        sprintf(position, "{\"x\":%d,\"y\":%d,\"z\":%d,\"facing\":%d}", s.x, s.y, s.z, s.facing);
    int used = sprintf(out,
        "{\"zone\":%d,\"room\":%d,\"start_state\":%d,\"music_id\":%d,\"music_slot\":%d,"
        "\"hp\":%d,\"max_hp\":%d,\"mp\":%d,\"max_mp\":%d,\"risk\":%d,\"game_over\":%d,"
        "\"intro_state\":%d,\"player\":%s}",
        s.zone, s.room, s.start_state, s.music_id, s.music_slot, s.hp, s.max_hp, s.mp, s.max_mp,
        s.risk, s.game_over, s.intro_state, position);
#ifdef VS_CALLO
    /* The Callo mod's companion (src/BATTLE/BATTLE.PRG/mod/merlose.h) */
    extern int vs_mod_describe(char*);
    used += sprintf(out + used - 1, ",\"callo\":") - 1;
    used += vs_mod_describe(out + used);
    out[used++] = '}';
    out[used] = 0;
#endif
    (void)used;
}
