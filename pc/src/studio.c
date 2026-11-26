/* The model studio, the game side (studio.h): the stage and the summoned model, built inside a photo mode
 * render (the state is rewound to the held moment when the render ends, so nothing here is undone).
 *
 * The stage is the loaded room's geometry block, overwritten in place with a lathe surface (a floor disc, a
 * quarter-circle fillet and a wall: a cyclorama) of untextured gouraud room polygons in the game's own
 * block format. Every consumer of the room (the native room list, the scene capture that feeds ray
 * tracing, the probes and shadows, the editor) then sees it as the room.
 *
 * The model is built as the Bestiary builds its enemies (MENU9 592C.c): the loader's object-data slots
 * (vs_battle_populateDataSlot) filled from files read by the host, pumped until the queue is empty. */
#include "game_pointer.h"
#include "psx_types.h"
#include "common.h"
#include "../../pc/src/game/BATTLE/BATTLE.PRG/146C.h"
#include "../../pc/src/game/BATTLE/BATTLE.PRG/2842C.h"
#include "../../pc/src/game/BATTLE/BATTLE.PRG/30DB0.h"
#include "../../pc/src/game/BATTLE/BATTLE.PRG/3A1A0.h"
#include "../../pc/src/game/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../pc/src/game/SLUS_010.40/main.h"
#include "studio.h"
#include "studio_stage.h"

/* The record's first member and the loader's weapon-slot byte are still named by their offsets in the headers (pc/names; the
 * PS1-ties lint counts those names): reached here by offsetof. */
#define BODY(a) ((vs_actors_ModelInstance*)((char*)(a) + __builtin_offsetof(D_800F4538_t, unk0)))
static void set_wep_byte(vs_battle_objectData* d, int v) { *((char*)d + __builtin_offsetof(vs_battle_objectData, unk11)) = (char)v; }

extern double floor(double), ceil(double), sin(double), cos(double);
extern void* memcpy(void*, const void*, unsigned long);
extern void* memset(void*, int, unsigned long);
extern int snprintf(char*, unsigned long, const char*, ...);

extern char vs_field_isSolidBackground;
extern char vs_field_textureEffectCount; /* the old room's texture-effect slots: their handlers write UV bytes at the old room's polygon offsets, which in the stage's block are vertex words */
extern unsigned char vs_field_isTiledBackgroundEnabled;

int pc_room_loaded(void);
const unsigned char* pc_room_geometry(unsigned* length);

int pc_studio_room_ready(void)
{
    unsigned length = 0;
    return pc_room_loaded() && pc_room_geometry(&length) && length >= 4096;
}

/* ---- the stage ---- (studio_stage.c builds the block; here it goes over the loaded room's) */

static pc_stage_cfg stage_cfg_of(const pc_studio_scene_cfg* c)
{
    pc_stage_cfg k;
    k.radius = c->radius, k.fillet = c->fillet, k.height = c->height, k.grey = c->grey, k.facet = c->facet;
    k.floor_step = c->floor_step, k.wall_step = c->wall_step;
    return k;
}
static unsigned build_hidden(float cx, float fy, float cz, char* why, int why_size, char* info, int info_size)
{
    unsigned length = 0;
    unsigned char* block = (unsigned char*)pc_room_geometry(&length);
    unsigned used = pc_stage_build_hidden(cx, fy, cz, block, length, why, why_size, info, info_size);
    if (used)
        vs_battle_roomData.header.textureEffectsSectionLen = 0;
    return used;
}
static unsigned build_stage(const pc_studio_scene_cfg* c, float cx, float fy, float cz, char* why, int why_size, char* info, int info_size)
{
    unsigned length = 0, need = 0;
    unsigned char* block = (unsigned char*)pc_room_geometry(&length);
    pc_stage_cfg k = stage_cfg_of(c);
    unsigned used = pc_stage_build(&k, cx, fy, cz, block, length, &need, why, why_size, info, info_size);
    if (!used && need > length) { /* the room's own block is too small: a block of the heap's (put back with the rest of the state) */
        unsigned char* big = vs_main_allocHeap(need);
        if (!big) {
            snprintf(why, (unsigned long)why_size, "stage needs %u bytes, the room's block is %u and the heap has no more", need, length);
            return 0;
        }
        vs_battle_roomData.geometrySection = (int*)big;
        vs_battle_roomData.header.geometrySectionLen = need;
        used = pc_stage_build(&k, cx, fy, cz, big, need, &need, why, why_size, info, info_size);
    }
    if (used)
        vs_battle_roomData.header.textureEffectsSectionLen = 0;
    return used;
}

/* ---- the model ---- */

static D_800F4538_t* actor_at(int slot)
{
    D_800F4538_t* a = vs_actors_actorRecords[slot];
    return pc_game_pointer_ok(a, sizeof *a) ? a : NULL;
}
/* A free actor slot the weapon records can follow (a slot s holds its weapon in wepModels[2s], 2s+1: slots 1..9). */
static int free_slot(void)
{
    for (int i = 9; i >= 1; i--)
        if (!actor_at(i))
            return i;
    return -1;
}
/* A free weapon record index at or above `from` (the actor slots below own theirs). */
static int pump(void)
{
    int guard = 0;
    while (vs_battle_processObjectDataQueue() != 0 && guard++ < 400)
        ;
    return guard < 400;
}

static int load_shp(int model_id, int slot, const unsigned char* shp, int material, void* buffer)
{
    vs_battle_objectData d;
    memset(&d, 0, sizeof d);
    d.dataType = 2;
    d.index = (signed char)slot;
    d.dataAddr = (__UINTPTR_TYPE__)shp;
    d.modelId = (short)model_id;
    d.destination = buffer;
    d.actorId = 0xFF;
    d.variant = 0;
    d.material = (char)material;
    vs_battle_populateDataSlot(&d);
    return pump();
}

static void* copy_to_heap(const unsigned char* file, unsigned size)
{
    unsigned char* mem = vs_main_allocHeapR(size);
    if (mem)
        memcpy(mem, file, size);
    return mem;
}

int pc_studio_scene(const pc_studio_scene_cfg* c, pc_studio_scene_out* out)
{
    memset(out, 0, sizeof *out);
    out->slot = out->weapon_slot = -1;
    D_800F4538_t* player = actor_at(0);
    if (!pc_studio_room_ready() || !player) {
        snprintf(out->message, sizeof out->message, "no room with a player to stand the stage in");
        return -1;
    }
    float cx = BODY(player)->position.vx, fy = BODY(player)->position.vy, cz = BODY(player)->position.vz;
    out->position[0] = cx, out->position[1] = fy, out->position[2] = cz;
    if (c->backdrop) { /* the game's background in one colour: its sky dome's five colours equal, its tiles off */
        int colours[5];
        for (int i = 0; i < 5; i++)
            colours[i] = c->backdrop_rgb & 0xffffff;
        vs_field_isTiledBackgroundEnabled = 0;
        vs_field_isSolidBackground = 0;
        vs_field_setRoomBackgroundColors(colours);
    }
    if (c->stage && c->backdrop) {
        if (!build_hidden(cx, fy, cz, out->message, sizeof out->message, out->info, sizeof out->info))
            return -1;
    } else if (c->stage && !build_stage(c, cx, fy, cz, out->message, sizeof out->message, out->info, sizeof out->info))
        return -1;
    if (c->stage)
        vs_field_textureEffectCount = 0; /* (put back by the render's rewind) */
    if (c->stage) { /* the room's light triangles gone: the models take one colour everywhere (table[0]) */
        unsigned* table = (unsigned*)vs_battle_roomData.lightingSection;
        if (pc_game_pointer_ok(table, 12))
            table[0] = (unsigned)c->actor_light, table[1] = table[2] = 0;
    }
    if (c->hide_others)
        for (int i = 0; i < 17; i++) {
            if (actor_at(i))
                BODY(actor_at(i))->skip = 1;
            else if (i < 16)
                vs_actors_setActorDisplayFlags(i, 0); /* no actor there: the room's object of that number (a save point, a chest), skipped */
        }
    if (c->kind == PC_STUDIO_NONE)
        return 0;
    int slot = free_slot();
    if (slot < 0) {
        snprintf(out->message, sizeof out->message, "no free actor slot (1..9)");
        return -1;
    }
    if (c->base) { /* a model that shares another's textures: that one first, hidden, under the id the SHP names it by */
        int base_slot = -1;
        for (int i = 15; i > 9 && base_slot < 0; i--)
            if (!actor_at(i))
                base_slot = i;
        unsigned char* base = copy_to_heap(c->base, c->base_size);
        void* base_buffer = vs_main_allocHeap(VS_OBJECT_REGION);
        if (base_slot < 0 || !base || !base_buffer || !load_shp(c->base_id, base_slot, base, 0, base_buffer)) {
            snprintf(out->message, sizeof out->message, "cannot load the model whose textures it shares (%d)", c->base_id);
            return -1;
        }
        BODY(actor_at(base_slot))->skip = 1;
    }
    unsigned char* file = copy_to_heap(c->file, c->size);
    unsigned char* rig = c->kind == PC_STUDIO_WEP && c->rig ? copy_to_heap(c->rig, c->rig_size) : NULL;
    void* buffer = vs_main_allocHeap(VS_OBJECT_REGION + 2 * sizeof(vs_battle_wepModels_t));
    if (!file || !buffer || (c->kind == PC_STUDIO_WEP && !rig)) {
        snprintf(out->message, sizeof out->message, "the heap is full");
        return -1;
    }
    if (c->kind == PC_STUDIO_SHP) {
        if (!load_shp(c->model_id, slot, file, c->material, buffer)) {
            snprintf(out->message, sizeof out->message, "the loader did not finish");
            return -1;
        }
    } else if (c->kind == PC_STUDIO_ZUD) {
        pc_nm_zud zud;
        pc_nm_zud_decode(&zud, file);
        if (!zud.shp.data || zud.shp.size < 0x100) {
            snprintf(out->message, sizeof out->message, "the ZUD holds no model");
            return -1;
        }
        if (!load_shp(c->model_id, slot, zud.shp.data, zud.defaultMaterial, buffer))
            return snprintf(out->message, sizeof out->message, "the loader did not finish"), -1;
        vs_battle_objectData d;
        memset(&d, 0, sizeof d);
        if (zud.weapon && c->equipment) {
            d.dataType = 4;
            d.index = (signed char)(slot * 2);
            d.dataAddr = (__UINTPTR_TYPE__)zud.weaponWep.data;
            d.modelId = zud.weapon;
            d.destination = (char*)buffer + VS_OBJECT_REGION;
            d.actorId = (u_char)slot;
            set_wep_byte(&d, 0xF0);
            d.material = (char)zud.weaponMaterial;
            vs_battle_populateDataSlot(&d);
        }
        if (zud.shield && c->equipment) {
            d.dataType = 4;
            d.index = (signed char)(slot * 2 + 1);
            d.dataAddr = (__UINTPTR_TYPE__)zud.shieldWep.data;
            d.modelId = zud.shield;
            d.destination = (char*)buffer + VS_OBJECT_REGION + sizeof(vs_battle_wepModels_t);
            d.actorId = (u_char)slot;
            set_wep_byte(&d, 0xF1);
            d.material = (char)zud.shieldMaterial;
            vs_battle_populateDataSlot(&d);
        }
        if (zud.battleSeq.size) {
            d.dataType = 8, d.index = (signed char)slot, d.dataAddr = (__UINTPTR_TYPE__)zud.battleSeq.data, d.actorId = 0;
            d.modelId = zud.weaponCategory;
            vs_battle_populateDataSlot(&d);
        }
        if (zud.commonSeq.size) {
            d.dataType = 8, d.index = (signed char)slot, d.dataAddr = (__UINTPTR_TYPE__)zud.commonSeq.data, d.actorId = 0;
            d.modelId = 0;
            vs_battle_populateDataSlot(&d);
        }
        pump();
    } else { /* a weapon: held by a rig model that is not drawn */
        if (!load_shp(c->model_id, slot, rig, 0, buffer)) {
            snprintf(out->message, sizeof out->message, "the loader did not finish");
            return -1;
        }
        vs_battle_objectData d;
        memset(&d, 0, sizeof d);
        d.dataType = 4;
        d.index = (signed char)(slot * 2);
        d.dataAddr = (__UINTPTR_TYPE__)file;
        d.modelId = (short)(c->model_id + 1);
        d.destination = (char*)buffer + VS_OBJECT_REGION;
        d.actorId = (u_char)slot;
        set_wep_byte(&d, 0xF0);
        d.material = (char)c->material;
        vs_battle_populateDataSlot(&d);
        pump();
    }
    D_800F4538_t* a = actor_at(slot);
    if (!a || !BODY(a)->model) {
        snprintf(out->message, sizeof out->message, "no actor after the load (slot %d)", slot);
        return -1;
    }
    BODY(a)->position.vx = (short)cx, BODY(a)->position.vy = (short)fy, BODY(a)->position.vz = (short)cz;
    BODY(a)->facing = (short)(c->facing & 0xfff);
    BODY(a)->skip = 0;
    BODY(a)->weaponDrawn = 1;
    out->slot = slot;
    out->bones = BODY(a)->nBones;
    /* shown, animated, its weapon and shield drawn (as the loader leaves an actor that a script has yet to show) */
    vs_actors_setActorDisplayFlags(slot, 1 | 2 | 4 | 0x800 | 0x1000);
    if (c->animation >= 0) {
        if (c->animation_frame > 0)
            vs_actors_playActorAnimationAtFrame(slot, c->animation, 0, c->animation_frame);
        else
            vs_actors_playActorAnimation(slot, c->animation, 0, 0);
    }
    out->animation = a->animationId;
    out->animation_frames = *(short*)((char*)a + __builtin_offsetof(D_800F4538_t, unk5BC)); /* (still named by its offset: pc/names) */
    return 0;
}

/* rood-studio (rood_studio.c) gives the scene capture, ray tracing and the probes a room: this block, as the loaded room's geometry (the same
 * words the in-game studio writes over a room's). Not a room's loading: nothing else of a room is set. */
void pc_studio_app_room(unsigned char* block, unsigned length)
{
    *((char*)&vs_battle_roomData + __builtin_offsetof(_mpdRoomSection, unk0)) = 1; /* (the room's first byte, still named by its offset: pc/names) */
    vs_battle_roomData.geometrySection = (int*)block;
    vs_battle_roomData.header.geometrySectionLen = length;
    vs_battle_roomData.header.textureEffectsSectionLen = 0;
}
