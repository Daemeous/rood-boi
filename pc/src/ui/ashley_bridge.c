/* Modern menus, Ashley tab: the game side (battle builds). menu_bridge.c
 * calls pc_ui_ashley_open when the shell takes over a menu session and
 * pc_ui_ashley_fill on every tick of the top level, before the shell's tick.
 *
 * What the snapshot (ui_model.h pc_ui_ashley) holds and where it comes from:
 *   HP / MP / RISK      Ashley's record (vs_battle_characterState->unk3C), as
 *                       MENU4's _renderBasicStats shows them
 *   STR / INT / AGL     the Status stats panel's EQP column (MAINMENU
 *                       vs_mainMenu_populateBaseStats + renderEquipStats for
 *                       actor 0): the record's current value (buffs in) plus
 *                       the accessory's
 *   limbs               limbs[] up to the first nameIndex 0; names from
 *                       BATTLE's vs_battle_statusStrings (as MENU4
 *                       _renderSelectedLimbCondition), state from
 *                       vs_battle_getLimbStatus
 *   effects             vs_battle_getStatusFlags' display bits in order (as
 *                       MENU4 _navigateStatusModifiers lists them); name and
 *                       description from MENU4's own status strings (the
 *                       game's text, generated from the disc at build time);
 *                       buff = MENU4's icon palette mask 0x0F0F906A; time
 *                       left from statusTimers (below)
 *   location            the live room (D_800F1AB0) named through
 *                       room_warp.h pc_rooms_* (looked up at open: the first
 *                       call reads the disc)
 *   play time           vs_main_gametime
 *   model               Ashley's menu clone (below, pc_ui_ashley_model_tick)
 *
 * The model. MENU4's Status screen shows Ashley through MAINMENU's menu clone
 * (224.c: func_800F9A24 builds it in actor slot 1, func_800F9EB8 animates and
 * draws it with a view matrix, func_800F9E0C frees it) and freezes the world
 * meanwhile (func_8008A4DC(0): vs_battle_exec then skips the sky, the actors'
 * animation and drawing, the load queue and its SetGeomOffset(160, 112);
 * D_800EB9AE = 1 keeps the field HUD's status line off). The Ashley tab does
 * the same while it shows:
 *   build   the tab shows, MAINMENU is resident (its code), the load queue is
 *           empty (func_800F9A78 spins on vs_battle_processObjectDataQueue
 *           until it drains: only its own entries may be in it), no shell
 *           result is held, the editor's free camera is off (func_8007ACB0
 *           would build our view from it) and the game heap has one free
 *           block for the whole clone (heap_need: func_800F9A78 uses its
 *           allocations unchecked, so a full heap would crash it): freeze,
 *           build, stand the clone at Ashley's place (place_clone), drop
 *           the game's rim copy (no_rim_copy: the studio light has its own)
 *           and ask for the window's resolution
 *           (pc_hw_detail_view, given back at teardown). Otherwise no model
 *           (the screen draws its normal backdrop and says why). The first
 *           tick does not draw (as MENU4's skipped frame).
 *   draw    every later tick: pump the load queue (frozen, the world does
 *           not), back up the world's camera, view matrix (and its copy
 *           D_8005E218), projection distance, then MENU4's orbit camera
 *           (_drawScreen, its xOffset 0, about the clone's place) with
 *           our yaw and pitch, our projection distance and a geometry offset
 *           that centres the model in the layout's model column
 *           (pc_ui_draw_model_column), func_8007ACB0 + func_800F9EB8, and the
 *           world's camera, view matrix, projection distance and
 *           SetGeomOffset(160, 112) back right after. The menu tick runs
 *           before vs_battle_exec's (gated) world draw and DrawOTag, as
 *           MENU4's does, so the clone goes into this frame's ordering table
 *           (actor_native.c reads the camera at each draw).
 *   tear    the tab no longer shows (tab change, Square's Status, close, a
 *           held hand-off), deliver, abandon: free the clone, restore, unfreeze.
 *           Before an original screen starts (MENU4 builds its own clone in
 *           slot 1). A soft reset only forgets (the heap is the next game's).
 * Turning: the right stick (the camera stick's settings) and the shell's
 * drag; never on its own. */
#include "common.h"
#include "../../../src/BATTLE/BATTLE.PRG/146C.h"
#include "../../../src/BATTLE/BATTLE.PRG/2842C.h"
#include "../../../src/BATTLE/BATTLE.PRG/3A1A0.h"
#include "../../../src/BATTLE/BATTLE.PRG/5BF94.h"
#include "../../../src/MENU/MAINMENU.PRG/224.h"
#include "../../../src/SLUS_010.40/main.h"
#include "build/assets/MENU/MENU4.PRG/status.h"
#include "scratch.h"
#include "gpu.h"
#include "../camera_stick.h"
#include "../room_warp.h"
#include "ui_shell.h"
#include <libgte.h>

/* ui_text.h (stdio in a game-header unit is avoided) */
int pc_ui_text_decode(const unsigned char* text, int max_bytes, char* out, int out_size);

void pc_ui_ashley_open(void);
void pc_ui_ashley_fill(void);
void pc_ui_ashley_model_open(int fresh);
void pc_ui_ashley_model_tick(int resident, int held);
void pc_ui_ashley_model_close(void);
void pc_ui_ashley_model_forget(void);

int pc_weapon_slots_menu_can_load(void); /* weapon_slots.c: the load queue is free */
extern int pc_gte_widescreen;            /* PsyX_GTE.cpp: 16:9 (layout 1280 wide) */
extern int D_800F190C; /* BATTLE 146C.c: the world runs (func_8008A4DC) */
extern MATRIX D_8005E218; /* func_8007ACB0 writes the view's rotation here too */
extern vs_main_HeapHeader heapA; /* main.c: the game heap's free list (vs_main_allocHeap) */
extern int pc_live_camera_active(void) __attribute__((weak)); /* editor_view.cpp: the free camera drives the view */
extern unsigned pc_vsync_count(void) __attribute__((weak));
extern int pc_window_stick(float out[2]) __attribute__((weak)); /* gamepad right stick and keys */
extern int pc_rpc_stick(float out[2]) __attribute__((weak));    /* RPC input {right_stick} */
/* gpu_hw.c: the internal resolution at least the window's while the model shows */
extern void pc_hw_detail_view(int on) __attribute__((weak));

/* MENU4.PRG's status strings (120.c _statusStrings), under our own name. */
static u_short status_text[] = {
#include "build/assets/MENU/MENU4.PRG/status.vsString"
};

extern _mpdRoomDoorSection_t D_800F1AB0; /* the live room (dev_state.c) */

#define EFFECT_COUNT 32
#define BUFF_MASK 0x0F0F906Au /* MENU4 _renderStatusIcons: set bit = the buff palette */

static pc_ui_ashley snap;

/* The model (see the file comment). */
#define YAW_START 0xB00 /* MENU4's _xPos: ONE / 16 * 11 */
#define PITCH_MAX 896
static struct {
    int state;         /* 0 none, 1 built (not drawn yet), 2 drawn */
    int yaw, pitch;    /* the orbit (4096 = a turn); kept across rebuilds in a menu session */
    float yaw_frac, pitch_frac;
    unsigned vsync;    /* pc_vsync_count at the last turn */
    float rect[4];     /* layout units: the model column */
    /* the world's: backed up before each draw, restored after it and at teardown */
    MATRIX view, view_copy;
    vs_camera_t camera;
    int projection;
    char frozen, status_line; /* D_800F190C and D_800EB9AE before the build */
    SVECTOR spot;      /* where the clone stands: Ashley's place in the room (place_clone) */
    int blocked;       /* PC_UI_MODEL_*: why there is no model */
    u_int need, largest; /* heap units (16 bytes): the clone's, the largest free block */
} model = { 0, YAW_START };

/* Tests (RPC ui_menu {model_heap_extra}): bytes the heap check asks for on
 * top of the clone's (a huge value plays a full heap). */
int pc_ui_ashley_heap_extra;
static struct {
    int zone, room; /* what area/room were looked up for; -1 none */
    char area[64], room_name[96];
} where = { -1, -1, "", "" };
static int effects_decoded;
static void publish(void);
static char effect_name[EFFECT_COUNT][32];
static char effect_help[EFFECT_COUNT][160];

/* UTF-8 text into out, cut at a code point boundary (as equip_bridge.c). */
static void copy_text(char* out, int size, const char* text)
{
    int i;
    for (i = 0; i + 1 < size && text[i]; ++i)
        out[i] = text[i];
    if (((unsigned char)text[i] & 0xC0) == 0x80) {
        while (i > 0 && ((unsigned char)out[i - 1] & 0xC0) == 0x80)
            --i;
        if (i > 0)
            --i;
    }
    out[i] = 0;
}

/* The game string at index i of a vsString table. */
static const u_char* table_string(const u_short* table, int i) { return (const u_char*)&table[table[i]]; }

/* Bytes before the string's end (0xE7) or its first line break (0xE8),
 * stepping over the operands of control bytes as pc_ui_text_decode does. */
static int first_line(const u_char* s, int max)
{
    int i = 0;
    while (i < max && s[i] != 0xE7 && s[i] != 0xE8) {
        if (s[i] == 0xFA || (s[i] >= 0xEC && s[i] != 0xFC))
            ++i;
        ++i;
    }
    return i < max ? i : max;
}

/* Each display bit's name (the first line) and description (the rest). */
static void decode_effects(void)
{
    int i, n;
    if (effects_decoded)
        return;
    for (i = 0; i < EFFECT_COUNT; ++i) {
        const u_char* s = table_string(status_text, VS_status_INDEX_strDown + i);
        n = first_line(s, 512);
        pc_ui_text_decode(s, n, effect_name[i], sizeof effect_name[i]);
        if (s[n] == 0xE8)
            pc_ui_text_decode(s + n + 1, 512, effect_help[i], sizeof effect_help[i]);
        else
            effect_help[i][0] = 0;
    }
    effects_decoded = 1;
}

/* statusTimers index of display bit i (vs_battle_getStatusFlags: display bits
 * 0..15 are the status word's bits 5..20, 16..31 its bits 21..28 spread by
 * element; the timers as func_80086FA8 sets them): 0 STR/INT/AGL up and down
 * (word 0x7E0), 1 quicken (0x810), 2 equip and elemental (0x1FF80000), 3
 * paralysis (0x2000), 4 numbness (0x8000); silence, poison (timer 5 is its
 * damage interval), curse, regeneration (likewise) and magic ward have none. */
static int timer_of(int bit)
{
    if (bit <= 5)
        return 0;
    switch (bit) {
    case 6: return 1;
    case 8: return 3;
    case 10: return 4;
    default: return bit >= 14 ? 2 : -1;
    }
}

static vs_battle_actor2* ashley(void)
{
    return vs_battle_characterState ? psx_get(vs_battle_characterState->unk3C) : NULL;
}

static void room_now(int* zone, int* room)
{
    const _mpdRoomDoorSection_t* location = D_800F1AB0.zndId ? &D_800F1AB0 : &D_80061068;
    *zone = location->zndId;
    *room = location->mpdId;
}

/* The shell takes over a menu session: name the room once (the room list is
 * built from the disc on its first use; the room does not change while the
 * menu shows). */
void pc_ui_ashley_open(void)
{
    int zone, room, i, n;
    room_now(&zone, &room);
    if (zone == where.zone && room == where.room)
        return;
    /* cached only once the list could be searched (else the next open asks again) */
    where.zone = where.room = -1;
    where.area[0] = where.room_name[0] = 0;
    if (!pc_rooms_count || !pc_rooms_get || (n = pc_rooms_count()) <= 0)
        return;
    where.zone = zone;
    where.room = room;
    for (i = 0; i < n; ++i) {
        const pc_room_info* r = pc_rooms_get(i);
        if (r && r->zone == zone && r->room == room) {
            copy_text(where.area, sizeof where.area, r->area);
            copy_text(where.room_name, sizeof where.room_name, r->name);
            break;
        }
    }
}

void pc_ui_ashley_fill(void)
{
    vs_battle_actor2* a = ashley();
    int i, flags;

    vs_main_bzero(&snap, sizeof snap);
    copy_text(snap.area, sizeof snap.area, where.area);
    copy_text(snap.room, sizeof snap.room, where.room_name);
    snap.play_h = (u_char)vs_main_gametime.t.h;
    snap.play_m = (u_char)vs_main_gametime.t.m;
    snap.play_s = (u_char)vs_main_gametime.t.s;
    snap.valid = a != NULL;
    snap.tick = pc_vsync_count ? pc_vsync_count() : 0;
    if (a) {
        snap.hp = a->currentHP;
        snap.hp_max = a->maxHP;
        snap.mp = a->currentMP;
        snap.mp_max = a->maxMP;
        snap.risk = a->risk;
        snap.str = a->strength + a->accessory.currentStr;
        snap.intel = a->intelligence + a->accessory.currentInt;
        snap.agl = a->agility + a->accessory.currentAgility;

        for (i = 0; i < PC_UI_LIMBS && a->limbs[i].nameIndex != 0; ++i) {
            vs_battle_uiEquipment_limb* limb = &a->limbs[i];
            pc_ui_limb* l = &snap.limbs[i];
            pc_ui_text_decode(table_string(vs_battle_statusStrings, limb->nameIndex), 64, l->name, sizeof l->name);
            l->hp = limb->hp;
            l->hp_max = limb->maxHp;
            l->state = vs_battle_getLimbStatus(limb);
        }
        snap.limb_count = i;

        decode_effects();
        flags = vs_battle_getStatusFlags(a);
        for (i = 0; i < EFFECT_COUNT && snap.effect_count < PC_UI_EFFECTS; ++i) {
            pc_ui_effect* e;
            int t;
            if (!(((u_int)flags >> i) & 1))
                continue;
            e = &snap.effects[snap.effect_count++];
            copy_text(e->name, sizeof e->name, effect_name[i]);
            copy_text(e->help, sizeof e->help, effect_help[i]);
            e->buff = (BUFF_MASK >> i) & 1;
            t = timer_of(i);
            e->seconds = t >= 0 && a->statusTimers[t] ? a->statusTimers[t] : -1;
        }
    }
    publish();
}

/* ---- the model ---- */

/* Layout (y) units per PS1 screen unit: 720 / 240 lines. In x the same:
 * 960 / 320 in 4:3, and in 16:9 the GTE narrows X by 3/4 about the screen
 * centre (PsyX_GTE.cpp) on a 1280-wide layout: 4 * 3/4. */
#define LAYOUT_PER_PIXEL 3.0f
/* The model's height on screen as a share of the column's. The projection
 * distance that gives it: MENU4's 512 shows Ashley ASHLEY_AT_512 PS1 lines
 * tall (measured from its Status screen), and the projected size grows
 * linearly with it. */
#define FILL 0.8f
#define ASHLEY_AT_512 145.0f
/* The orbit looks at the model's middle by its own height offset; Ashley's
 * outline (antennae to soles) is centred this share of her height below it. */
#define LOOK_UP 0.09f

static void backup_world_view(void)
{
    model.view = vs_scratch.viewMatrix;
    model.view_copy = D_8005E218;
    model.camera = vs_scratch.camera;
    model.projection = vs_main_projectionDistance;
}

static void restore_world_view(void)
{
    vs_scratch.camera = model.camera;
    vs_scratch.viewMatrix = model.view;
    D_8005E218 = model.view_copy;
    vs_battle_setProjectionDistance(model.projection);
    SetGeomOffset(160, 112);
}

/* The orbit camera: MENU4 _drawScreen with its xOffset 0 (the look-at point
 * on the model's axis), so the model sits at the geometry offset; about the
 * clone's place (place_clone), where MENU4 looks at the origin. */
static void place_camera(void)
{
    D_800F4538_t* clone = D_800F4538[1];
    vs_camera_t* camera = &vs_scratch.camera;
    int z = clone->menuCameraDistance;
    int x = clone->menuCameraHeightOffset;
    int t;

    camera->lookAt.vx = model.spot.vx * ONE;
    camera->lookAt.vy = model.spot.vy * ONE - (x * ONE / 2);
    camera->lookAt.vz = model.spot.vz * ONE;
    camera->position.vx = camera->lookAt.vx + ((rcos(model.yaw) * rcos(model.pitch)) >> 0xA) * z;
    camera->position.vy = camera->lookAt.vy - rsin(model.pitch) * z * 4;
    t = ((rsin(model.yaw) * rcos(model.pitch)) >> 0xA) * z;
    camera->position.vz = camera->lookAt.vz + t;
    camera->angles.vx = camera->angles.vy = camera->angles.vz = 0;
    camera->farClip = ONE;
}

/* The clone where Ashley stands (func_800F9CB0 puts it at the world's
 * origin, and MENU4's camera looks there). The renderer lights the model
 * with its studio light (pc_hw_present_menu_studio); with that off
 * (RPC graphics menu_studio 0) in world space, as the field: the room's
 * lights, shadows, global illumination and haze about Ashley's place light
 * the clone as they light him (the origin is anywhere in a room, often
 * outside it: dark, or in a wall). The orbit (place_camera) circles the same
 * place. */
static void place_clone(void)
{
    D_800F4538_t* clone = D_800F4538[1];
    D_800F4538_t* actor = D_800F4538[0];
    model.spot.vx = model.spot.vy = model.spot.vz = 0;
    if (!clone || !actor)
        return;
    model.spot.vx = actor->unk0.position.vx;
    model.spot.vy = actor->unk0.position.vy;
    model.spot.vz = actor->unk0.position.vz;
    clone->unk0.position.vx = model.spot.vx;
    clone->unk0.position.vy = model.spot.vy;
    clone->unk0.position.vz = model.spot.vz;
}

/* The game's rim: func_800F9CB0 gives the clone a second copy of itself
 * (func_8009F940: unk17FD 1, unk6F4 = 2, -2, 2 world units off and 16 + 48
 * ordering-table entries behind; func_800F9EB8 draws it after the clone in
 * its own colour), a lit edge where it peeks out. At the window's
 * resolution it reads as bright slivers, and the renderer lights the model
 * with a studio rig of its own (gpu_hw.c pc_hw_present_menu_studio,
 * menu_studio.glsl) with its own rim: the copy goes, as the game itself
 * drops it (30DB0.c: unk17FD 1 back to 0). */
extern int pc_hw_menu_studio __attribute__((weak)); /* gpu_hw.c */
static void no_rim_copy(void)
{
    D_800F4538_t* clone = D_800F4538[1];
    if (clone && clone->unk17FD == 1 && &pc_hw_menu_studio && pc_hw_menu_studio)
        clone->unk17FD = 0;
}

/* A full-screen tile behind everything this frame (the ordering table's
 * back slot, where MENU4 _drawMenuBackground links its picture): frozen, the
 * world draws nothing and the frame buffer keeps old frames (the model would
 * smear as it turns). Full width, so it covers the 16:9 view too (gpu_hw.c).
 * The theme's ground colour (ui_theme.cpp). The presenters show the
 * picture of the world they kept under the menu's ground in place of this
 * tile (gl_present.c "The modern menu's backdrop"; frames tagged through
 * pc_ui_ashley_ot_model), so the tab is see-through like the others; the
 * tile itself shows only when no picture was kept. */
static int ot_model; /* this tick's ordering table holds the tile (and the model): pc_ui_ashley_ot_model */
static void clear_behind(void)
{
    u_long* prim = psx_get(vs_scratch.unk0);
    void* ot = psx_get(vs_scratch.unk4);
    u_int* back = ot + 0x1FF4;
    int r, g, b;

    pc_ui_theme_ground_rgb(&r, &g, &b);
    prim[0] = (*back & 0xFFFFFF) | (3 << 24);
    prim[1] = vs_getRGB0(primTile, r, g, b);
    prim[2] = vs_getXY(0, 0);
    prim[3] = vs_getWH(320, 240);
    *back = (psx_to_int(u_long, prim) << 8) >> 8;
    psx_set(vs_scratch.unk0, prim + 4);
    ot_model = 1;
}

/* The battle loop drew an ordering table (boot_runtime.c
 * pc_battle_frame_complete, right after its DrawOTag): 1 when it was one of
 * the model view's (the tile, the model), once. gpu_hw.c tags that frame:
 * presents that show it run the post stack neutral (the model is the only
 * 3D in it) and show the held picture of the world where the tile is
 * (gl_present.c / vk_present.c backdrop), so the tab is see-through like the
 * others while the post stack never touches the model. */
int pc_ui_ashley_ot_model(void)
{
    int on = ot_model;
    ot_model = 0;
    return on;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The right stick (as camera_stick.c reads it: the window's and the RPC's)
 * shaped with the camera stick's settings, plus the shell's drag. */
static void turn(void)
{
    float stick[2] = { 0, 0 }, v[2], dx, dy, x, y, dt, step;
    unsigned t = pc_vsync_count ? pc_vsync_count() : 0, gap = t - model.vsync;
    int k, n;
    model.vsync = t;
    dt = (gap < 1 ? 1 : gap > 8 ? 2 : (float)gap) / 59.94f;
    if (pc_window_stick && pc_window_stick(v))
        stick[0] += v[0], stick[1] += v[1];
    /* The window/input recorder already combines the RPC stick. */
    if (!pc_window_stick && pc_rpc_stick && pc_rpc_stick(v))
        stick[0] += v[0], stick[1] += v[1];
    for (k = 0; k < 2; ++k)
        stick[k] = clampf(stick[k], -1, 1);
    x = pc_camera_stick_shape(stick[0], pc_camera_stick_deadzone, pc_camera_stick_curve);
    y = pc_camera_stick_shape(stick[1], pc_camera_stick_deadzone, pc_camera_stick_curve);
    if (pc_camera_stick_invert_x)
        x = -x;
    if (pc_camera_stick_invert_y)
        y = -y;
    pc_ui_shell_model_turn(&dx, &dy);
    step = pc_camera_stick_speed * (4096.0f / 360.0f) * dt;
    /* Stick right / drag right: the model turns right (its front follows the
     * pointer); stick down / drag down: the view rises above her. 1 layout
     * unit of drag = 8 angle units. */
    model.yaw_frac += -x * step - dx * 8.0f;
    model.pitch_frac += y * step + dy * 8.0f;
    n = (int)model.yaw_frac;
    model.yaw_frac -= (float)n;
    model.yaw = (model.yaw + n) & 0xFFF;
    n = (int)model.pitch_frac;
    model.pitch_frac -= (float)n;
    model.pitch += n;
    if (model.pitch < 0 || model.pitch > PITCH_MAX) {
        model.pitch = model.pitch < 0 ? 0 : PITCH_MAX;
        model.pitch_frac = 0;
    }
}

/* ---- the game heap ---- */

/* Room left for allocations the count below does not know about. */
#define HEAP_MARGIN 0x1000

/* What vs_main_allocHeap takes for size bytes: 16-byte units plus its header. */
static u_int heap_units(u_int size) { return ((size + 15) >> 4) + 1; }

/* The largest free block in heap units, walking the free list as
 * vs_main_allocHeap does (circular through heapA, whose own size is 0). */
static u_int heap_largest(void)
{
    vs_main_HeapHeader* h = psx_get(heapA.next);
    u_int best = 0;
    int guard;
    for (guard = 0; h && h != &heapA && guard < 0x10000; ++guard) {
        if (h->blockSz > best)
            best = h->blockSz;
        h = psx_get(h->next);
    }
    return best;
}

/* What func_800F9A24(0) allocates (func_800F9A78: the clone record; a second
 * record when actor 0 has a linked one (unk17FD >= 2); per held weapon its
 * record and, from _parseWep's copy for slots 2 and 3, its offsets buffer;
 * the animation buffer when actor 0 is slot 0's own), plus a margin, in heap
 * units; 0 without actor 0. One free block of this size serves them all: the
 * allocator is first fit, so smaller blocks earlier in the list only help. */
static u_int clone_units(void)
{
    D_800F4538_t* a = D_800F4538[0];
    u_int n;
    int i;
    if (!a)
        return 0;
    n = heap_units(0x1900);
    if (a->unk17FD >= 2)
        n += heap_units(0x1900);
    for (i = 0; i < 2; ++i) {
        int k = a->unk0.unkF * 2 + i;
        if (k < 20 && vs_battle_wepModels[k])
            n += heap_units(sizeof(vs_battle_wepModels_t)) + heap_units(sizeof(_wepModelOffsets));
    }
    if (a->unk0.unkF == 0)
        n += heap_units(0x7800);
    return n + (HEAP_MARGIN + (u_int)(pc_ui_ashley_heap_extra > 0 ? pc_ui_ashley_heap_extra : 0)) / 16;
}

/* Free the clone and give the world back. */
static void backdrop_reset(void);

void pc_ui_ashley_model_close(void)
{
    backdrop_reset(); /* every session (and a return from an original screen) waits its full time */
    if (!model.state)
        return;
    if (D_800F4538[1])
        func_800F9E0C();
    if (pc_hw_detail_view)
        pc_hw_detail_view(0);
    restore_world_view();
    func_8008A4DC(model.frozen);
    D_800EB9AE = model.status_line;
    model.state = 0;
    publish();
}

/* A soft reset: the heap and the world are the next game's; nothing to give
 * back. The view starts over at the next session. */
void pc_ui_ashley_model_forget(void)
{
    backdrop_reset(); /* a soft reset inside the wait: the next game's first session waits afresh */
    if (model.state && pc_hw_detail_view)
        pc_hw_detail_view(0);
    model.state = 0;
    model.blocked = PC_UI_MODEL_OK;
    model.yaw = YAW_START;
    model.pitch = 0;
    model.yaw_frac = model.pitch_frac = 0;
    publish();
}

/* The shell (re)opens the top level (menu_bridge.c pc_ui_menu_open): a
 * leftover clone goes (it never should be there); a new menu session
 * (fresh) starts the view over, a return from an original screen keeps it. */
void pc_ui_ashley_model_open(int fresh)
{
    pc_ui_ashley_model_close();
    if (fresh) {
        model.yaw = YAW_START;
        model.pitch = 0;
        model.yaw_frac = model.pitch_frac = 0;
    }
}

/* This tick's model state into the snapshot the shell shows (the fill ran
 * before the shell's tick, this after it). */
static void publish(void)
{
    snap.model = model.state;
    snap.model_yaw = model.yaw;
    snap.model_pitch = model.pitch;
    snap.model_blocked = model.state ? PC_UI_MODEL_OK : model.blocked;
    snap.heap_free = (int)(heap_largest() * 16);
    snap.heap_need = (int)(model.need * 16);
    if (model.state)
        vs_main_memcpy(snap.model_rect, model.rect, sizeof snap.model_rect);
    else
        vs_main_bzero(snap.model_rect, sizeof snap.model_rect);
    pc_ui_shell_set_ashley(&snap);
}

/* The presenters keep a picture of the world on each world present while
 * the menu is up and show it, under the menu's ground at its opacity,
 * behind the model (gl_present.c / vk_present.c "The modern menu's
 * backdrop"); once the model is built every frame is the model's and none
 * is kept. So the build waits (at most BACKDROP_WAIT ticks) for a picture
 * taken with the field HUD slid out: two kept after the last tick that
 * drew the HUD (a present may show an earlier tick's frame). Without it a
 * menu opened straight onto this tab, or back from an original screen
 * (MAINMENU resident: the build on the first tick), showed the ground
 * alone (opaque, whatever ui_menu_opacity) or the HUD in the picture. */
extern int pc_hw_active __attribute__((weak));                          /* gpu_hw.c */
extern int pc_gl_menu_backdrop_kept(void) __attribute__((weak));       /* 0: none, else a count */
extern int pc_vk_menu_backdrop_kept(void) __attribute__((weak));
enum { BACKDROP_WAIT = 20 };
static int backdrop_ticks, backdrop_need;
static int backdrop_seen;              /* the keep count backdrop_ready last saw */
static int built_kept = -1, built_need; /* at the last model build (RPC ui_menu backdrop_built; -1: none yet) */
static void backdrop_reset(void) { backdrop_ticks = backdrop_need = 0; }
static int backdrop_ready(void)
{
    int kept = (pc_gl_menu_backdrop_kept ? pc_gl_menu_backdrop_kept() : 0)
             + (pc_vk_menu_backdrop_kept ? pc_vk_menu_backdrop_kept() : 0);
    backdrop_seen = kept;
    if (!&pc_hw_active || !pc_hw_active || ++backdrop_ticks > BACKDROP_WAIT)
        return 1; /* (no presenter: nothing to keep) */
    if (D_800EB9B0 != 0x200000)
        backdrop_need = kept + 2;
    return kept && kept >= backdrop_need;
}

/* Tests (RPC ui_menu backdrop_built): the presenter's keep count and the
 * count the wait asked for when the model was last built (kept -1: none
 * built yet). A build over a kept, HUD-free picture has kept >= need > 0. */
void pc_ui_ashley_backdrop_built(int* kept, int* need)
{
    *kept = built_kept;
    *need = built_need;
}

/* Every top-level tick, after the shell's (menu_bridge.c pc_ui_menu_top):
 * resident: MAINMENU drives the menu; held: a shell result waits. */
void pc_ui_ashley_model_tick(int resident, int held)
{
    float W, ofx, ofy, h;
    int live = pc_live_camera_active && pc_live_camera_active();

    if (!pc_ui_shell_wants_model() || held || live) {
        model.blocked = live ? PC_UI_MODEL_FREE_CAMERA : PC_UI_MODEL_OK;
        pc_ui_ashley_model_close(); /* publishes when there was a model */
        backdrop_reset();
        publish();
        return;
    }
    pc_ui_draw_model_column(model.rect);
    if (!model.state) {
        if (!resident || !pc_weapon_slots_menu_can_load() || D_800F4538[1] || !D_800F4538[0]
            || !backdrop_ready()) {
            publish();
            return;
        }
        built_kept = backdrop_seen, built_need = backdrop_need;
        backdrop_reset();
        model.need = clone_units();
        model.largest = heap_largest();
        if (model.largest < model.need) { /* the build would run out of heap: none (tried again next tick) */
            model.blocked = PC_UI_MODEL_NO_HEAP;
            publish();
            return;
        }
        model.blocked = PC_UI_MODEL_OK;
        backup_world_view();
        model.frozen = D_800F190C != 0;
        model.status_line = D_800EB9AE;
        func_8008A4DC(0);
        D_800EB9AE = 1;
        func_800F9A24(0);
        place_clone();
        no_rim_copy();
        /* Drawn from the next tick at the window's resolution: this frame
         * (the back tile only) takes the change. */
        if (pc_hw_detail_view)
            pc_hw_detail_view(1);
        model.vsync = pc_vsync_count ? pc_vsync_count() : 0;
        model.state = 1; /* drawn from the next tick (MENU4 skips a frame too) */
        clear_behind();
        publish();
        return;
    }
    if (!D_800F4538[1]) { /* something else freed it */
        pc_ui_ashley_model_close();
        return;
    }
    /* Frozen, vs_battle_exec does not drain the load queue. */
    if (vs_battle_objectDataSlots[0].dataType)
        vs_battle_processObjectDataQueue();
    turn();
    clear_behind();

    /* The model column's centre as a geometry offset (layout x = W/2 +
     * 3 (sx - 160), y = 3 sy); its height sets the projection distance. */
    W = pc_gte_widescreen ? 1280.0f : 960.0f;
    ofx = 160.0f + (model.rect[0] + model.rect[2] * 0.5f - W * 0.5f) / LAYOUT_PER_PIXEL;
    ofy = (model.rect[1] + model.rect[3] * (0.5f - LOOK_UP * FILL)) / LAYOUT_PER_PIXEL;
    h = 512.0f * (model.rect[3] * FILL / LAYOUT_PER_PIXEL) / ASHLEY_AT_512;

    /* The world's view as it is now (frozen, nothing should change it, but
     * whatever did between two ticks is what teardown gives back). Order per
     * frame: this menu tick, then vs_battle_exec's (frozen) world, which uses
     * the restored view; func_8007ACB0 writes the view matrix and its copy
     * D_8005E218, both restored right after the draw. */
    backup_world_view();
    vs_battle_setProjectionDistance((int)(h + 0.5f));
    place_camera();
    SetGeomOffset((int)(ofx + 0.5f), (int)(ofy + 0.5f));
    func_8007ACB0();
    func_800F9EB8(&vs_scratch.viewMatrix);
    restore_world_view();
    model.state = 2;
    publish();
}
