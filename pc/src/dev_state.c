/* Vagrant Story snapshot for the developer RPC `state` method (dev_rpc.c).
 * Compiled with the game headers so field offsets follow the decompilation. */
#include "game_pointer.h"
#include "psx_types.h"
#include "common.h"
#include "../../pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "../../pc/src/game/BATTLE/BATTLE.PRG/3A1A0.h"
#include "../../pc/src/game/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../pc/src/game/BATTLE/BATTLE.PRG/target_block.h"
#include "../../pc/src/game/SLUS_010.40/main.h"

extern int vs_main_startState;
/* 146C.h: the room the player is in (the last door's destination). INITBTL
 * starts it from vs_state_savedEntryDoor, which then holds the saved location only: the
 * game copies the current room back into it when saving. */
/* 146C.h: vs_battle_actors[0] is Ashley; its unk3C points at the HUD record
 * (name[24], then currentHP, maxHP, currentMP, maxMP, risk as shorts). */
extern int sprintf(char*, const char*, ...);

#include "../../pc/src/game/BATTLE/BATTLE.PRG/ai_state.h"
#include "dev_state.h"
/* Actor records live in the guest heap; during the attract demo and room
 * transitions the slots can hold stale values, so only follow pointers that
 * lie in guest RAM (this runs every frame for the lights). */
void pc_dev_state(struct pc_dev_state* out)
{
    D_800F4538_t* player = vs_actors_actorRecords[0];
    vs_battle_actor* actor = vs_battle_actors[0];
    short* stats = NULL;
    if (!pc_game_pointer_ok(player, sizeof *player))
        player = NULL;
    if (pc_game_pointer_ok(actor, sizeof *actor) && pc_game_pointer_ok(actor->unk3C, 0x24))
        stats = (short*)((unsigned char*)actor->unk3C + 0x18);
    out->valid = 1;
    const _mpdRoomDoorSection_t* location = vs_field_pendingDestinationDoor.zndId ? &vs_field_pendingDestinationDoor : &vs_state_savedEntryDoor;
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
/* The room's characters (dev_state.h): the actor slots vs_actors_actorRecords[0..16]
 * that point into guest RAM; the radius is their drop shadow's
 * (shadowRadius, the field actor_shadow.c reads at 0x64c). */
int pc_dev_actors(struct pc_dev_actor* out, int max)
{
    int n = 0;
    for (int i = 0; i < 17 && n < max; i++) {
        D_800F4538_t* actor = vs_actors_actorRecords[i];
        if (!pc_game_pointer_ok(actor, sizeof *actor))
            continue;
        out[n].slot = i;
        out[n].pos[0] = actor->unk0.position.vx;
        out[n].pos[1] = actor->unk0.position.vy;
        out[n].pos[2] = actor->unk0.position.vz;
        out[n].radius = actor->shadowRadius;
        out[n].facing = actor->unk0.facing & 0xfff;
        out[n].weapon_drawn = actor->unk0.weaponDrawn == 1;
        n++;
    }
    return n;
}
/* The actor slot whose record holds p (its model), or whose held weapon or shield record
 * (vs_battle_wepModels: drawn by vs_actors_buildWeaponBoneChain from the holder's bone; their own records, outside the
 * actor's) does while photo_ui.cpp actors_held is on; -1 none (actor_native.c: rood.ui.hide{actors}). */
extern int pc_photo_actors_held __attribute__((weak));
int pc_dev_actor_of(const void* p)
{
    for (int i = 0; i < 17; i++) {
        D_800F4538_t* actor = vs_actors_actorRecords[i];
        if (pc_game_pointer_ok(actor, sizeof *actor) && (const char*)p >= (const char*)actor
            && (const char*)p < (const char*)actor + sizeof *actor)
            return i;
    }
    if (!&pc_photo_actors_held || !pc_photo_actors_held)
        return -1;
    for (int i = 0; i < 20; i++) {
        vs_battle_wepModels_t* held = vs_battle_wepModels[i];
        if (pc_game_pointer_ok(held, sizeof *held) && (const char*)p >= (const char*)held
            && (const char*)p < (const char*)held + sizeof *held)
            return held->actorId;
    }
    return -1;
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

/* RPC battle_state: the fields that make the battle's behaviour, by name, for every actor slot: the actor record,
 * its stats, its object and its AI state. A struct change moves them with it, so a comparison of two builds
 * (pc/tools/build_compare.py) needs no offsets. Pointers are left out (their values differ with the layout). */
extern vs_battle_aiState* vs_battle_aiActorAiStates[];
extern int randIndex;
struct json {
    char* p;
    char* end;
};
static void put(struct json* j, const char* key, long value)
{
    if (j->p + 48 < j->end)
        j->p += sprintf(j->p, "\"%s\":%ld,", key, value);
}
#define F(j, o, field) put(j, #field, (long)(o)->field)
/* The effect runtime (the script nodes of vs_effects_primaryInstances and their particles), by value: a 64-bit FNV-1a of each node's
 * records and, per particle, its numbers as text. Pointers are left out, so two builds with different layouts
 * agree when the effects do (build_compare.py compares them step by step). */
static unsigned long long fnv(const void* data, unsigned size)
{
    unsigned long long h = 1469598103934665603ull;
    for (unsigned i = 0; i < size; ++i)
        h = (h ^ ((const unsigned char*)data)[i]) * 1099511628211ull;
    return h;
}
static void effect_nodes(struct json* j)
{
    j->p += sprintf(j->p, ",\"effects\":[");
    int count = 0;
    for (vs_effects_EffectInstance* n = vs_effects_primaryInstances; n && count < 64 && pc_game_pointer_ok(n, sizeof *n); n = n->next, ++count) {
        if (j->p + 8192 > j->end)
            break;
        j->p += sprintf(j->p, "{");
        F(j, n, unk8); F(j, n, unk9); put(j, "unkA", n->cursor); /* the key keeps the old name: build_compare compares it across builds */
        put(j, "word14", *(unsigned*)(n->unk10 + 4));
        for (int i = 0; i < 4; ++i)
            j->p += sprintf(j->p, "\"unk10_%d\":%d,", i, n->unk10[i]);
        for (int i = 0; i < 16; ++i) {
            func_800CE714_t2_2* e = &n->unk1C[i];
            j->p += sprintf(j->p, "\"entry%d\":\"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%llx,%llx\",", i, e->unk0, e->unk4,
                e->unk8, e->unkC, e->unk10, e->unk14, e->unk18, e->unk1C, e->unk20, e->unk24, e->unk28, e->unk2C,
                e->unk30, e->unk34, e->unk35, e->unk36, e->unk37, e->unkBE, fnv(&e->unk38, sizeof e->unk38 + sizeof e->unk58),
                fnv(e->unk78, sizeof e->unk78 + 2 + sizeof e->unkC0));
        }
        put(j, "t3a", n->context.effectId); put(j, "t3b", n->context.unk3A); put(j, "t3c", n->context.unk3C);
        j->p += sprintf(j->p, "\"particles\":[");
        int k = 0;
        for (func_800D2904_t* p = n->unk18; p && k < 96 && pc_game_pointer_ok(p, sizeof *p); p = p->next, ++k) {
            if (j->p + 512 > j->end)
                break;
            j->p += sprintf(j->p, "\"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%llx,%u,%d,%d,%d,%d,%d,%d\",",
                p->unk8, p->unkA, p->unkC, p->unk10, p->unk14, p->unk18[0], p->unk18[1], p->unk18[2], p->unk24[0],
                p->unk24[1], p->unk24[2], p->unk30[0], p->unk30[1], p->unk30[2], p->unk5C, p->lifetime,
                fnv(&p->sprite, 0x18), p->unk60_0 | p->unk60_8 << 8 | p->unk60_12 << 12, p->unk62[0], p->unk62[1], p->unk62[2], p->unk6A,
                p->endEvent, p->tickEvent);
        }
        j->p += sprintf(j->p, "\"end\"]},");
    }
    j->p += sprintf(j->p, "{}]");
}
void pc_rpc_battle_state(char* out, unsigned size)
{
    struct json j = { out, out + size };
    j.p += sprintf(j.p, "{\"rand\":%d,\"actors\":[", randIndex);
    for (int i = 0; i < 17; ++i) {
        vs_battle_actor* a = i < 16 ? vs_battle_actors[i] : NULL;
        D_800F4538_t* o = vs_actors_actorRecords[i];
        vs_battle_aiState* s = i < 16 ? vs_battle_aiActorAiStates[i] : NULL;
        if (!pc_game_pointer_ok(a, sizeof *a))
            a = NULL;
        if (!pc_game_pointer_ok(o, sizeof *o))
            o = NULL;
        if (!pc_game_pointer_ok(s, sizeof *s))
            s = NULL;
        if (!a && !o && !s)
            continue;
        j.p += sprintf(j.p, "{");
        if (a) {
            j.p += sprintf(j.p, "\"actor\":{");
            F(&j, a, id); F(&j, a, unk8); F(&j, a, unk9); F(&j, a, unkA); F(&j, a, unkB); F(&j, a, unkC);
            put(&j, "unk10", a->unk10.s32); F(&j, a, unk14); F(&j, a, unk18); F(&j, a, subType); F(&j, a, unk1C);
            F(&j, a, weaponDrawn); F(&j, a, defeated); put(&j, "flags30", a->flags30.raw); F(&j, a, unk34);
            F(&j, a, unk38); F(&j, a, unk40);
            if (pc_game_pointer_ok(a->unk3C, sizeof *a->unk3C)) {
                vs_battle_actor2* t = a->unk3C;
                F(&j, t, currentHP); F(&j, t, currentMP); F(&j, t, statuses);
            }
            j.p += sprintf(j.p, "\"end\":0},");
        }
        if (o) {
            j.p += sprintf(j.p, "\"object\":{");
            put(&j, "x", o->unk0.position.vx); put(&j, "y", o->unk0.position.vy); put(&j, "z", o->unk0.position.vz);
            put(&j, "angleX", o->unk0.rotationX); F(&j, &o->unk0, facing); put(&j, "angleZ", o->unk0.rotationZ);
            put(&j, "tileX", o->unk0.currentTileX); put(&j, "tileZ", o->unk0.currentTileZ);
            put(&j, "flags8", *(unsigned*)((char*)&o->unk0 + 8)); put(&j, "flagsC", *(unsigned*)((char*)&o->unk0 + 0xC));
            put(&j, "flags5AC", VS_OBJECT_FLAGS5AC(o)); F(&j, o, animationId); F(&j, o, unk5B9); F(&j, o, unk5BA);
            F(&j, o, unk5BC); F(&j, o, unk6E6); F(&j, o, unk17FD); F(&j, o, unk18D2); F(&j, o, unk18D4);
            F(&j, &o->unkC54, unk540); F(&j, &o->unkC54, unk542); F(&j, &o->unk704, unk540); F(&j, &o->unk704, unk542);
            put(&j, "unk6EE", o->unk6EE); put(&j, "unk6EF", o->unk6EF);
            j.p += sprintf(j.p, "\"end\":0},");
        }
        D_800F45E0_t* q = i < 16 ? vs_field_gimmickInstances[i] : NULL;
        if (pc_game_pointer_ok(q, sizeof *q)) {
            j.p += sprintf(j.p, "\"model\":{");
            F(&j, q, unk0); F(&j, q, unk1); F(&j, q, unk12); F(&j, q, unk13); F(&j, q, unk1A); F(&j, q, unk1C);
            F(&j, q, unk1E); F(&j, q, unk20); F(&j, q, unk22); F(&j, q, unk24); F(&j, q, unk26);
            put(&j, "flags8", *(unsigned*)((char*)q + 8)); put(&j, "flagsC", *(unsigned*)((char*)q + 0xC));
            put(&j, "flags5AC", *(unsigned*)((char*)&q->unk6C + sizeof q->unk6C));
            j.p += sprintf(j.p, "\"end\":0},");
        }
        if (s) {
            j.p += sprintf(j.p, "\"ai\":{");
            put(&j, "flags0", s->flags0.word); put(&j, "flags4", s->flags4.word); put(&j, "flags8", s->flags8.word);
            put(&j, "flagsC", s->flagsC.word); F(&j, s, flagE); F(&j, s, flagF); F(&j, s, unk10); F(&j, s, unk11);
            F(&j, s, unk12); F(&j, s, flag13); F(&j, s, unk14); F(&j, s, target16); F(&j, s, maxActionCost);
            F(&j, s, unk18); F(&j, s, dead); F(&j, s, flag20); F(&j, s, f21); F(&j, s, f23); F(&j, s, f24);
            F(&j, s, allowFallback); F(&j, s, field2A); F(&j, s, special); F(&j, s, f2C); F(&j, s, f2D);
            put(&j, "tile", s->tile.raw); F(&j, s, unk3C); F(&j, s, unk40); F(&j, s, options); F(&j, s, timer44);
            put(&j, "px", s->position.xyz[0]); put(&j, "py", s->position.xyz[1]); put(&j, "pz", s->position.xyz[2]);
            F(&j, s, radius); F(&j, s, timer8E); F(&j, s, timer98); F(&j, s, rangeSquared); F(&j, s, step);
            F(&j, s, distance); F(&j, s, delay); F(&j, s, rate); F(&j, s, row); F(&j, s, unk124); F(&j, s, unk125);
            F(&j, s, mode12D); F(&j, s, timer13A); F(&j, s, heading); F(&j, s, moveX);
            j.p += sprintf(j.p, "\"end\":0},");
        }
        j.p += sprintf(j.p, "\"slot\":%d},", i);
    }
    j.p += sprintf(j.p, "{}]");
    effect_nodes(&j);
    j.p += sprintf(j.p, "}");
}

/* The loaded room's blocks for the host units (room_effects.c, scene_capture.c): the geometry block and the
 * texture-effects section with their lengths, and whether a room is loaded. */
int pc_room_loaded(void)
{
    return vs_battle_roomData.unk0 == 1;
}
const unsigned char* pc_room_geometry(unsigned* length)
{
    *length = vs_battle_roomData.header.geometrySectionLen;
    return (const unsigned char*)vs_battle_roomData.geometrySection;
}
const unsigned char* pc_room_effects(unsigned* length)
{
    *length = vs_battle_roomData.header.textureEffectsSectionLen;
    return (const unsigned char*)vs_battle_roomData.textureEffectsSection;
}
const unsigned* pc_room_light_table(int* enabled)
{
    *enabled = (signed char)vs_battle_roomData.unk0;
    return (const unsigned*)vs_battle_roomData.lightingSection;
}
