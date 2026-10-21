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
/* The room's characters (dev_state.h): the actor slots D_800F4538[0..16]
 * that point into guest RAM; the radius is their drop shadow's
 * (shadowRadius, the field actor_shadow.c reads at 0x64c). */
int pc_dev_actors(struct pc_dev_actor* out, int max)
{
    int n = 0;
    for (int i = 0; i < 17 && n < max; i++) {
        D_800F4538_t* actor = D_800F4538[i];
        if (!guest_pointer(actor, sizeof *actor))
            continue;
        out[n].slot = i;
        out[n].pos[0] = actor->unk0.position.vx;
        out[n].pos[1] = actor->unk0.position.vy;
        out[n].pos[2] = actor->unk0.position.vz;
        out[n].radius = actor->shadowRadius;
        out[n].facing = actor->unk0.facing & 0xfff;
        n++;
    }
    return n;
}
/* scene_submit.inc (in BATTLE's 146C.c, which knows the action record). */
extern int pc_battle_action(int* actor, int* action, int* phase, int* chain, int* type, int* plain, int aim[3],
    int* target);
int pc_dev_battle_action(struct pc_dev_action* out)
{
    int aim[3];
    if (!pc_battle_action(&out->actor, &out->action, &out->phase, &out->chain, &out->type, &out->plain, aim,
            &out->target))
        return 0;
    out->aim[0] = (float)aim[0], out->aim[1] = (float)aim[1], out->aim[2] = (float)aim[2];
    return 1;
}
extern void pc_battle_camera_focus(int out[14]);
int pc_dev_battle_camera(struct pc_dev_camera* out)
{
    int v[14];
    pc_battle_camera_focus(v);
    if (v[0] < 0)
        return 0;
    out->state = v[0], out->mode = v[1];
    out->candidate = v[2], out->area = v[6], out->candidate_actor = v[2] ? v[13] : -1;
    for (int i = 0; i < 3; i++) {
        out->candidate_pos[i] = (float)v[3 + i];
        out->area_pos[i] = (float)v[7 + i];
        out->look_at[i] = (float)v[10 + i];
    }
    return 1;
}
void pc_rpc_state(char* out, unsigned size)
{
    struct pc_dev_state s = { 0 };
    char position[96] = "null";
    (void)size; /* the reply buffer is far larger than this fixed-size object */
    pc_dev_state(&s);
    if (s.has_player)
        sprintf(position, "{\"x\":%d,\"y\":%d,\"z\":%d,\"facing\":%d}", s.x, s.y, s.z, s.facing);
    sprintf(out,
        "{\"zone\":%d,\"room\":%d,\"start_state\":%d,\"music_id\":%d,\"music_slot\":%d,"
        "\"hp\":%d,\"max_hp\":%d,\"mp\":%d,\"max_mp\":%d,\"risk\":%d,\"game_over\":%d,"
        "\"intro_state\":%d,\"player\":%s}",
        s.zone, s.room, s.start_state, s.music_id, s.music_slot, s.hp, s.max_hp, s.mp, s.max_mp,
        s.risk, s.game_over, s.intro_state, position);
}
