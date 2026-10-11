/* Room texture effects as animated materials.
 *
 * A room's texture effects (MPD room section 0xC, set up by func_80092548
 * and run every game frame through D_800E85CC, BATTLE 2842C.c) include two
 * kinds that rewrite one polygon's UV bytes in the room data:
 *
 * - type 0, func_80091C6C: UV atlas frames. Every delay + 1 ticks the next
 *   frame; the polygon's UVs become base UVs + (baseU + frameWidth * (frame %
 *   columns), baseV + frameHeight * (frame / columns)).
 * - type 2, func_80091E10: UV scroll. Every delay + 1 ticks the window
 *   position (currentU, currentV) moves by (stepU, stepV), wrapping inside
 *   the region baseU..baseU + width (the window, windowWidth wide, stays in
 *   it); the polygon's UVs become base UVs + the position. On the disc: the
 *   save point's sigil and light column (1590 entries in 372 rooms).
 *
 * Both write u8 sums: uv = (base uv + offset) & 255 per vertex. The native
 * room's meshes keep the base UVs and a per-polygon effect index (render.h
 * render_polygon.uv_effect); each frame this module reads the offset the
 * game wrote into the polygon (checked against every vertex, so what is
 * drawn is exactly the packet path's frame) and passes one offset per effect
 * to the room's draw list (render_list_uv_effects): the shaders add it per
 * vertex. The game's own state drives it, so its timing, pauses, starts and
 * stops and save/load stay in step; nothing is re-uploaded.
 *
 * For frame interpolation each effect also carries the motion since the
 * previous game frame, unwrapped across the scroll's wrap (the steps from
 * the window base to where it wraps: the period); between game frames the
 * shaders draw the offset partway and wrap texels that run past the region
 * back by the period.
 *
 * An effect whose polygon's bytes do not match base UVs + one offset (game
 * code rewriting them otherwise, a polygon outside the room's groups, a
 * code byte the effect treats as the other polygon kind) is not modelled:
 * its polygon keeps its bytes in the mesh and room_native.c's mesh_follow
 * re-decodes the block when they change (the fallback for any rewrite).
 *
 * The ray-traced scene (scene_capture.c) takes the drawn UVs each frame
 * (pc_scene_uv_refresh), so bounce light and glows see the current texels.
 *
 * Overrides for the editor (Details > Inspector; saved with the room's
 * lights file): a speed multiplier (the effect then runs on its own phase,
 * advanced by the game's motion times the multiplier) and freeze. */
#define _POSIX_C_SOURCE 200809L
#include "room_effects.h"
#include "psx_types.h"
#include "render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern unsigned char vs_battle_roomData[] __attribute__((weak));
/* The game's effect slots: 12 bytes each (active, type, enabled, -, entry,
 * -); D_800F225A: slots in use. */
extern unsigned char D_800F1DD8[] __attribute__((weak));
extern char D_800F225A __attribute__((weak));
void pc_scene_uv_refresh(void);

#define ROOM_GEOMETRY 0x64 /* vs_battle_roomData.geometrySection */
#define ROOM_EFFECTS 0x90 /* .textureEffectsSection */
#define ROOM_EFFECTS_LEN 0x30 /* .header.textureEffectsSectionLen */

/* 0: no effect is modelled (every rewrite re-decodes the meshes, the
 * behaviour before animated materials; for comparisons). ROOD_UV_EFFECTS
 * sets it at startup; RPC graphics uv_effects. */
int pc_uv_effects_enabled = -1;
static pc_uv_effect effects[PC_UV_EFFECTS_MAX];
static int effect_count;
static const void* parsed_data;
static unsigned parsed_signature;
static unsigned generation = 1;
static uint32_t updated_serial = 0xffffffffu;
static render_uv_effect table[PC_UV_EFFECTS_MAX];
/* Per effect: 0 not drawn yet, 1 following the game, 2 on its own phase
 * (texels from the window base; speed overridden), 3 frozen. */
static int mode[PC_UV_EFFECTS_MAX];
static unsigned char found[PC_UV_EFFECTS_MAX]; /* its polygon is one of the room's */
static float phase[PC_UV_EFFECTS_MAX][2];
static struct {
    int set, freeze;
    float speed;
} overrides[PC_UV_EFFECTS_MAX];

/* The corners' UV byte offsets in a polygon record (BATTLE 2842C.c). */
static const int u_byte[4] = { 0x18, 0x1c, 0x13, 0x23 }, v_byte[4] = { 0x19, 0x1d, 0x17, 0x27 };

static const unsigned char* effect_section(unsigned* length)
{
    *length = *(const unsigned*)(vs_battle_roomData + ROOM_EFFECTS_LEN);
    return (int)*length > 0 ? psx_load(const unsigned char*, vs_battle_roomData + ROOM_EFFECTS) : NULL;
}
/* The effect entries' fields that never change while the room is shown
 * (positions and tick counters do). */
static unsigned section_signature(const unsigned char* section, unsigned length)
{
    static const int sizes[7] = { 24, 20, 32, 0, 0, 0, 36 };
    unsigned hash = 2166136261u ^ length;
    for (unsigned at = 0; section && at < length;) {
        const unsigned char* e = section + at;
        unsigned type = e[6];
        if (type >= 7 || !sizes[type])
            break;
        /* the kind, and for the UV effects their polygon and window (the
         * other kinds' fields all move) */
        static const int bytes[] = { 6, 0, 1, 2, 3, 8, 9, 10, 11 };
        int n = type == 0 || type == 2 ? (int)(sizeof bytes / sizeof *bytes) : 1;
        for (int i = 0; i < n; i++)
            hash = (hash ^ e[bytes[i]]) * 16777619u;
        at += (unsigned)sizes[type];
    }
    return hash;
}
/* Is `offset` a polygon of one of the block's group geometry blocks? Sets
 * whether it is a quad. */
static int find_polygon(const unsigned char* data, unsigned offset, int* quad)
{
    unsigned groups = *(const unsigned*)data;
    for (unsigned g = 0; g < groups && g < 4096; g++) {
        unsigned block = *(const unsigned*)(data + 4 + g * 0x40 + 0x1c);
        const unsigned* geometry = (const unsigned*)(data + block);
        unsigned tris = geometry[0], quads = geometry[1];
        if (tris > 65535 || quads > 65535 || offset < block + 8)
            continue;
        unsigned at = offset - (block + 8);
        if (at < tris * 32) {
            if (at % 32)
                return 0;
            *quad = 0;
            return 1;
        }
        at -= tris * 32;
        if (at < quads * 40) {
            if (at % 40)
                return 0;
            *quad = 1;
            return 1;
        }
    }
    return 0;
}
static void parse(const unsigned char* data, const unsigned char* section, unsigned length)
{
    static const int sizes[7] = { 24, 20, 32, 0, 0, 0, 36 };
    effect_count = 0;
    unsigned geometry_length = *(const unsigned*)(vs_battle_roomData + 4);
    int slot = 0;
    for (unsigned at = 0; section && at < length; slot++) {
        const unsigned char* e = section + at;
        unsigned type = e[6];
        if (type >= 7 || !sizes[type])
            break;
        at += (unsigned)sizes[type];
        if ((type != 0 && type != 2) || effect_count == PC_UV_EFFECTS_MAX)
            continue;
        found[effect_count] = 0;
        pc_uv_effect* x = &effects[effect_count++];
        memset(x, 0, sizeof *x);
        x->type = (int)type;
        x->slot = slot;
        x->polygon = *(const unsigned*)e;
        x->delay = e[7];
        x->speed = 1;
        x->running = -1;
        if (type == 0) {
            x->base[0] = e[8], x->base[1] = e[9];
            x->frames = e[10], x->columns = e[11];
            x->frame_size[0] = e[12], x->frame_size[1] = e[13];
            memcpy(x->u, e + 16, 4);
            memcpy(x->v, e + 20, 4);
        } else {
            x->base[0] = e[8], x->base[1] = e[9];
            x->size[0] = e[10], x->size[1] = e[11];
            x->window[0] = e[14], x->window[1] = e[15];
            x->step[0] = (signed char)e[16], x->step[1] = (signed char)e[17];
            memcpy(x->u, e + 20, 4);
            memcpy(x->v, e + 24, 4);
            for (int a = 0; a < 2; a++) {
                int s = abs(x->step[a]), room = x->size[a] - x->window[a];
                x->period[a] = s && room >= 0 ? (room / s + 1) * s : 0;
            }
        }
        /* The polygon must be one of the room's (the effect writes the bytes
         * of the kind its code byte says; ours must agree). */
        int quad = 0;
        if ((int)x->polygon >= 0 && x->polygon + 40 <= geometry_length && find_polygon(data, x->polygon, &quad)) {
            x->quad = quad;
            found[effect_count - 1] = 1;
            int game_quad = (data[x->polygon + 0xf] & 0x3c) != 0x34;
            x->modelled = game_quad == quad && pc_uv_effects_enabled;
        }
    }
    memset(mode, 0, sizeof mode);
    ++generation;
    int modelled = 0;
    for (int i = 0; i < effect_count; i++)
        modelled += effects[i].modelled;
    fprintf(stderr, "[render] room texture effects: %d UV effects (%d modelled as animated materials)\n", effect_count,
        modelled);
}
/* The offset in the polygon's bytes; 0 when they are not base + one offset. */
static int read_offset(const unsigned char* data, const pc_uv_effect* x, int offset[2])
{
    const unsigned char* p = data + x->polygon;
    offset[0] = (p[u_byte[0]] - x->u[0]) & 255;
    offset[1] = (p[v_byte[0]] - x->v[0]) & 255;
    for (int k = 1; k < (x->quad ? 4 : 3); k++)
        if (((x->u[k] + offset[0]) & 255) != p[u_byte[k]] || ((x->v[k] + offset[1]) & 255) != p[v_byte[k]])
            return 0;
    return 1;
}
/* The scroll's motion from `was` to `now` on one axis, unwrapped (the
 * scroll wraps to the window base): 0 when it is not a few steps forward. */
static float motion(const pc_uv_effect* x, int a, int was, int now)
{
    int s = x->step[a], d = now - was, p = x->period[a];
    if (!s || !d)
        return 0;
    if (s > 0 && d < 0)
        d += p;
    if (s < 0 && d > 0)
        d -= p;
    if ((long)d * s <= 0 || abs(d) > 8 * abs(s) || (p && abs(d) >= p))
        return 0;
    return (float)d;
}
static void update(const unsigned char* data)
{
    int slots = &D_800F225A ? D_800F225A : -1;
    for (int i = 0; i < effect_count; i++) {
        pc_uv_effect* x = &effects[i];
        const unsigned char* e = NULL;
        if (D_800F1DD8 && slots >= 0 && x->slot < slots) {
            const unsigned char* slot = D_800F1DD8 + x->slot * 12;
            x->running = slot[0] && slot[2];
            e = psx_load(const unsigned char*, slot + 4);
        }
        if (e) {
            x->tick = x->type == 0 ? e[14] : e[18];
            if (x->type == 0)
                x->frame = e[15];
        }
        int now[2];
        if (!x->modelled) {
            /* (what the game wrote, for the record) */
            if (found[i])
                read_offset(data, x, x->game_offset);
            x->offset[0] = x->game_offset[0], x->offset[1] = x->game_offset[1];
            continue;
        }
        if (!read_offset(data, x, now)) {
            /* Rewritten otherwise: the meshes re-decode its bytes from now on. */
            x->modelled = 0;
            ++generation;
            fprintf(stderr, "[render] room texture effect %d (slot %d) not modelled: its polygon's UVs are rewritten "
                "otherwise\n", i + 1, x->slot);
            continue;
        }
        float d[2] = { 0, 0 }, lead[2] = { 0, 0 };
        if (x->type == 2 && mode[i])
            for (int a = 0; a < 2; a++)
                d[a] = motion(x, a, x->game_offset[a], now[a]);
        /* The tick counter's way to the next step (a scroll stepping every
         * delay + 1 ticks moves step / (delay + 1) per tick). */
        if (x->type == 2 && e && x->tick <= x->delay)
            for (int a = 0; a < 2; a++)
                lead[a] = (float)x->step[a] * (float)x->tick / (float)(x->delay + 1);
        if (!mode[i])
            x->offset[0] = now[0], x->offset[1] = now[1]; /* the first frame: nothing drawn before */
        x->game_offset[0] = now[0], x->game_offset[1] = now[1];
        x->speed = overrides[i].set ? overrides[i].speed : 1;
        x->freeze = overrides[i].set && overrides[i].freeze;
        for (int a = 0; a < 2; a++) {
            /* (a jump the scroll's motion does not explain: no motion) */
            int moved = mode[i] == 1 && (d[a] != 0 || now[a] == x->offset[a]);
            x->smooth[a] = moved ? d[a] + lead[a] - x->lead[a] : 0;
            x->lead[a] = lead[a];
        }
        if (x->freeze) {
            /* The offset drawn stays. */
            x->shift[0] = x->shift[1] = 0;
            x->lead[0] = x->lead[1] = x->smooth[0] = x->smooth[1] = 0;
            mode[i] = 3;
        } else if (x->speed != 1 && x->type == 2) {
            /* Its own phase (texels from the window base), advanced by the
             * game's motion times the speed. */
            for (int a = 0; a < 2; a++) {
                if (mode[i] != 2)
                    phase[i][a] = (float)(x->offset[a] - x->base[a]);
                if (!x->step[a] || !x->period[a]) {
                    x->offset[a] = now[a];
                    x->shift[a] = 0;
                    continue;
                }
                float m = d[a] * x->speed;
                phase[i][a] = fmodf(phase[i][a] + m, (float)x->period[a]);
                if (phase[i][a] < 0)
                    phase[i][a] += (float)x->period[a];
                x->offset[a] = (x->base[a] + (int)floorf(phase[i][a])) & 255;
                x->shift[a] = m;
                x->lead[a] = 0, x->smooth[a] = m;
            }
            mode[i] = 2;
        } else {
            x->offset[0] = now[0], x->offset[1] = now[1];
            x->shift[0] = mode[i] == 1 ? d[0] : 0, x->shift[1] = mode[i] == 1 ? d[1] : 0;
            mode[i] = 1;
        }
    }
    for (int i = 0; i < effect_count; i++) {
        const pc_uv_effect* x = &effects[i];
        render_uv_effect* t = &table[i];
        memset(t, 0, sizeof *t);
        if (!x->modelled)
            continue;
        for (int a = 0; a < 2; a++) {
            t->offset[a] = (int16_t)x->offset[a];
            t->shift[a] = x->shift[a];
            t->lead[a] = x->lead[a];
            t->smooth[a] = x->smooth[a];
            t->base[a] = (uint8_t)x->base[a];
            if (x->type == 2 && x->step[a] && x->period[a]) {
                t->size[a] = (uint16_t)x->size[a];
                t->period[a] = (uint16_t)x->period[a];
            }
        }
    }
}

int pc_uv_effects_frame(const void* data)
{
    if (!vs_battle_roomData || !data)
        return 0;
    if (pc_uv_effects_enabled < 0) {
        const char* env = getenv("ROOD_UV_EFFECTS");
        pc_uv_effects_enabled = !env || atoi(env) != 0;
    }
    const unsigned char* geometry = psx_load(const unsigned char*, vs_battle_roomData + ROOM_GEOMETRY);
    if (geometry != data)
        return effect_count; /* another block (func_8008E19C's) */
    unsigned length;
    const unsigned char* section = effect_section(&length);
    unsigned signature = section_signature(section, length) ^ (unsigned)(uintptr_t)section
        ^ (pc_uv_effects_enabled ? 0x9e3779b9u : 0);
    if (data != parsed_data || signature != parsed_signature) {
        parsed_data = data;
        parsed_signature = signature;
        parse(data, section, length);
        updated_serial = render_frame_serial() - 1;
    }
    uint32_t serial = render_frame_serial();
    if (serial != updated_serial) {
        updated_serial = serial;
        update(data);
        pc_scene_uv_refresh();
    }
    return effect_count;
}
const pc_uv_effect* pc_uv_effects(int* count)
{
    *count = parsed_data ? effect_count : 0;
    return *count ? effects : NULL;
}
const render_uv_effect* pc_uv_effects_table(int* count)
{
    *count = parsed_data ? effect_count : 0;
    return table;
}
int pc_uv_effect_at(const void* data, unsigned offset)
{
    if (data != parsed_data)
        return 0;
    for (int i = 0; i < effect_count; i++)
        if (effects[i].polygon == offset && effects[i].modelled)
            return i + 1;
    return 0;
}
unsigned pc_uv_effects_generation(void) { return generation; }

/* ---- overrides ---- */

void pc_uv_effect_override(int effect, float speed, int freeze)
{
    if (effect < 1 || effect > PC_UV_EFFECTS_MAX)
        return;
    int i = effect - 1;
    if (!(speed >= 0 && speed <= 64))
        speed = 1;
    overrides[i].set = speed != 1 || freeze;
    overrides[i].speed = speed;
    overrides[i].freeze = freeze != 0;
}
void pc_uv_effects_reset(void)
{
    memset(overrides, 0, sizeof overrides);
}
int pc_uv_effects_parse(const char* line)
{
    int index, freeze;
    float speed;
    if (sscanf(line, "uv_effect %d %f %d", &index, &speed, &freeze) != 3)
        return 0;
    pc_uv_effect_override(index, speed, freeze);
    return 1;
}
void pc_uv_effects_write(FILE* f)
{
    for (int i = 0; i < PC_UV_EFFECTS_MAX; i++)
        if (overrides[i].set)
            fprintf(f, "uv_effect %d %g %d\n", i + 1, overrides[i].speed, overrides[i].freeze);
}
int pc_uv_effects_overrides(void)
{
    int n = 0;
    for (int i = 0; i < PC_UV_EFFECTS_MAX; i++)
        n += overrides[i].set;
    return n;
}
