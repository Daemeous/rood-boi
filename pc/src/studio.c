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

/* ---- the stage ---- */

static void put32(unsigned char* b, unsigned at, unsigned v) { memcpy(b + at, &v, 4); }
static int snap8(double v) { return (int)(floor(v / 8.0 + 0.5)) * 8; }

/* The stage's facets are small and none is thinner than the grid the game keeps its facets on: a room facet's corners are a
 * position and steps of 8 units from it, so a ring whose neighbours are a few units apart collapses to nothing or turns
 * inside out (and is not drawn), and the renderer drops a facet that is large on the screen it is seen from. The rings of the
 * profile are therefore each given the fewest facets (a power of two, so each ring has twice or as many as the one inside it)
 * that keep them no wider than `facet` units, and a ring that has twice the facets of the one inside it is joined to it with
 * three triangles for each of the inner ring's edges. */
typedef struct {
    int rings, segs[160];
    double radius[160], height[160];
} profile_t;

static int ring_segments(double radius, double facet, int least)
{
    int n = least;
    while (n < 512 && 6.2831853 * radius / n > facet)
        n *= 2;
    return n;
}

static void stage_point(const profile_t* p, int ring, int j, short out[3])
{
    double a = 6.2831853 * (j % p->segs[ring]) / p->segs[ring];
    out[0] = (short)snap8(p->radius[ring] * cos(a));
    out[2] = (short)snap8(p->radius[ring] * sin(a));
    out[1] = (short)-snap8(p->height[ring]);
}

/* One facet at `q`: the first corner, the steps to the others (8 units each), the colours (a triangle's record is 32 bytes,
 * a quad's 40). */
static unsigned char* put_facet(unsigned char* q, const short v[4][3], int quad, unsigned colour)
{
    memcpy(q, v[0], 6);
    for (int k = 1; k < (quad ? 4 : 3); k++)
        for (int d = 0; d < 3; d++)
            ((signed char*)q)[(quad && k == 3 ? 32 : 6 + (k - 1) * 3) + d] = (signed char)((v[k][d] - v[0][d]) / 8);
    for (int k = 0; k < (quad ? 4 : 3); k++)
        put32(q, (unsigned)(quad && k == 3 ? 36 : 12 + k * 4), colour | (k == 0 ? (quad ? 0x38u : 0x30u) << 24 : 0));
    return q + (quad ? 40 : 32);
}

/* No stage: a block with one facet that faces away from every view (nothing is drawn, the scene still has a room). */
static unsigned build_hidden(float cx, float fy, float cz, char* why, int why_size, char* info, int info_size)
{
    unsigned length = 0;
    unsigned char* block = (unsigned char*)pc_room_geometry(&length);
    unsigned geom_at = 4 + 0x40, need = geom_at + 8 + 32;
    if (need > length) {
        snprintf(why, (unsigned long)why_size, "the room's block is too small for an empty stage");
        return 0;
    }
    memset(block, 0, need);
    put32(block, 0, 1);
    unsigned char* g = block + 4;
    put32(g, 0, 0xff);
    put32(g, 4, (unsigned)(int)cx), put32(g, 8, (unsigned)(int)fy), put32(g, 12, (unsigned)(int)cz);
    put32(g, 0x1c, geom_at);
    put32(block + geom_at, 0, 1), put32(block + geom_at, 4, 0);
    short v[4][3] = { { 0, 3000, 0 }, { 0, 3000, 8 }, { 8, 3000, 0 }, { 0, 0, 0 } }; /* (below the floor, wound to face down) */
    put_facet(block + geom_at + 8, v, 0, 0x606060);
    vs_battle_roomData.header.textureEffectsSectionLen = 0;
    snprintf(info, (unsigned long)info_size, "no stage: a solid backdrop");
    return need;
}

static unsigned build_stage(const pc_studio_scene_cfg* c, float cx, float fy, float cz, char* why, int why_size, char* info, int info_size)
{
    unsigned length = 0;
    unsigned char* block = (unsigned char*)pc_room_geometry(&length);
    double radius = c->radius, fillet = c->fillet, height = c->height;
    double facet = c->facet < 16 ? 16 : c->facet;
    if (radius < 100 || fillet < 1 || height < fillet) {
        snprintf(why, (unsigned long)why_size, "stage: radius >= 100, 1 <= fillet <= height");
        return 0;
    }
    /* the profile: the floor (rings every `floor_step`), the fillet's quarter circle, the wall (rings every `wall_step`) */
    static profile_t p;
    p.rings = 0;
    int floor_steps = (int)ceil(radius / (c->floor_step > 20 ? c->floor_step : 450.0));
    if (floor_steps > 60)
        floor_steps = 60;
    for (int i = 0; i <= floor_steps; i++)
        p.radius[p.rings] = radius * i / floor_steps, p.height[p.rings++] = 0;
    for (int i = 1; i <= 8; i++) {
        double a = 1.5707963 * i / 8;
        p.radius[p.rings] = radius + fillet * sin(a), p.height[p.rings++] = fillet * (1 - cos(a));
    }
    int wall_steps = (int)ceil((height - fillet) / (c->wall_step > 20 ? c->wall_step : 800.0));
    if (wall_steps > 60)
        wall_steps = 60;
    for (int i = 1; i <= wall_steps; i++)
        p.radius[p.rings] = radius + fillet, p.height[p.rings++] = fillet + (height - fillet) * i / wall_steps;
    p.segs[0] = 1;
    for (int i = 1; i < p.rings; i++) {
        int n = ring_segments(p.radius[i], facet, 8);
        int inside = i > 1 ? p.segs[i - 1] : 8;
        if (n > inside * 2)
            n = inside * 2; /* (a ring is at most twice as fine as the one inside it) */
        if (n < inside)
            n = inside;
        p.segs[i] = n;
    }
    unsigned tris = (unsigned)p.segs[1], quads = 0;
    for (int i = 1; i + 1 < p.rings; i++) {
        if (p.segs[i + 1] == p.segs[i])
            quads += (unsigned)p.segs[i];
        else
            tris += 3u * (unsigned)p.segs[i];
    }
    unsigned geom_at = 4 + 0x40, need = geom_at + 8 + tris * 32 + quads * 40;
    if (need > length) { /* the room's own block is too small: a block of the heap's (put back with the rest of the state) */
        unsigned char* big = vs_main_allocHeap(need);
        if (!big) {
            snprintf(why, (unsigned long)why_size, "stage needs %u bytes, the room's block is %u and the heap has no more", need, length);
            return 0;
        }
        vs_battle_roomData.geometrySection = (int*)big;
        vs_battle_roomData.header.geometrySectionLen = need;
        block = big, length = need;
    }
    snprintf(info, (unsigned long)info_size, "stage %u of %u bytes: %u triangles, %u quads, %d rings", need, length, tris, quads, p.rings);
    memset(block, 0, need);
    put32(block, 0, 1); /* one geometry group */
    unsigned char* g = block + 4;
    put32(g, 0, 0xff); /* flags: every facing bit, not hidden */
    put32(g, 4, (unsigned)(int)cx);
    put32(g, 8, (unsigned)(int)fy);
    put32(g, 12, (unsigned)(int)cz);
    put32(g, 0x1c, geom_at);
    unsigned char* geo = block + geom_at;
    put32(geo, 0, tris), put32(geo, 4, quads);
    unsigned char* poly = geo + 8;
    unsigned grey = (unsigned)(c->grey < 0 ? 0 : c->grey > 255 ? 255 : c->grey);
    unsigned col = grey | grey << 8 | grey << 16;
    short v[4][3];
    /* the triangles: the fan about the centre, then the joins between rings of different fineness */
    memset(v[0], 0, sizeof v[0]);
    for (int j = 0; j < p.segs[1]; j++) {
        stage_point(&p, 1, j + 1, v[1]);
        stage_point(&p, 1, j, v[2]);
        poly = put_facet(poly, v, 0, col);
    }
    for (int i = 1; i + 1 < p.rings; i++) {
        if (p.segs[i + 1] == p.segs[i])
            continue;
        for (int j = 0; j < p.segs[i]; j++) {
            short a0[3], a1[3], b0[3], b1[3], b2[3];
            stage_point(&p, i, j, a0), stage_point(&p, i, j + 1, a1);
            stage_point(&p, i + 1, 2 * j, b0), stage_point(&p, i + 1, 2 * j + 1, b1), stage_point(&p, i + 1, 2 * j + 2, b2);
            memcpy(v[0], a0, 6), memcpy(v[1], b1, 6), memcpy(v[2], b0, 6);
            poly = put_facet(poly, v, 0, col);
            memcpy(v[0], a0, 6), memcpy(v[1], a1, 6), memcpy(v[2], b1, 6);
            poly = put_facet(poly, v, 0, col);
            memcpy(v[0], a1, 6), memcpy(v[1], b2, 6), memcpy(v[2], b1, 6);
            poly = put_facet(poly, v, 0, col);
        }
    }
    /* the quads between rings of the same fineness */
    for (int i = 1; i + 1 < p.rings; i++) {
        if (p.segs[i + 1] != p.segs[i])
            continue;
        for (int j = 0; j < p.segs[i]; j++) {
            stage_point(&p, i, j, v[0]), stage_point(&p, i, j + 1, v[1]);
            stage_point(&p, i + 1, j, v[2]), stage_point(&p, i + 1, j + 1, v[3]);
            poly = put_facet(poly, v, 1, col);
        }
    }
    vs_battle_roomData.header.textureEffectsSectionLen = 0;
    return need;
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
        vs_zud_file zud;
        vs_zud_decode(&zud, file);
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
    if (c->animation >= 0)
        vs_actors_playActorAnimation(slot, c->animation, 0, 0);
    return 0;
}
