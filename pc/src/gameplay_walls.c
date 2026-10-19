/* Smart cutaway walls for gameplay (`gameplay_walls`, Native_Rendering.md
 * "Smart walls").
 *
 * The game hides every wall group whose facing mask lacks one of the game
 * camera's three bits (func_8009723C), whether or not the group stands
 * between the camera and Ashley. Rooms that are not a simple box then lose
 * walls that block nothing: a wall behind crates leaves black void, a wall
 * beside Ashley is not drawn while she walks into it. With gameplay_walls 1
 * ("smart") func_8009723C asks here about each group the mask leaves out
 * and draws it unless it gets in the way:
 *
 * - once per frame, the room's triangles (scene_capture.c, world space; the
 *   ray-traced scene's) are rasterised on the CPU into a coarse depth grid
 *   (80 x 60 cells over the 320 x 240 screen) as the game camera sees them:
 *   front faces as the PS1 tests them (and two-sided polygons), opaque, the
 *   groups the game's own test keeps forming the "base" picture;
 * - a masked group (not a black shell, not hidden by the room's flags)
 *   *occludes* when a front face of it lies in the focus region nearer
 *   than the focus: Ashley's body, a capsule from her feet to her head
 *   widened by gameplay_walls_radius (and in battle the camera's target, a
 *   sphere), projected on the screen; or when more than a few of its
 *   cells would be seen (over the base picture or over void) more than 96
 *   units nearer than the focus: in front of the play area (a near wall,
 *   its trims and columns). Seen over void at or beyond the play area (the
 *   black outside the room where a far wall belongs) is what it is drawn
 *   for; behind the base picture it hides nothing;
 * - hysteresis: a group counts as occluding from the first frame it
 *   occludes and stops after gameplay_walls_clear (10) frames clear of a
 *   region 1.3 times as wide; its drawn share fades (an ordered dither,
 *   render_instance.cut) out over gameplay_walls_fade_out (4) game frames
 *   and in over gameplay_walls_fade_in (7), interpolated between game frames
 *   by the renderer (cut_previous);
 * - occluding groups are left out, or with gameplay_walls_ghost drawn as
 *   ghosts (gameplay_walls_opacity of their pixels and their edges, as the
 *   editor's cut to focus draws them).
 *
 * Black shells stay hidden (the void outside the room stays black); a wall
 * group's back faces are culled as the PS1 culls them, so the outside of a
 * revealed wall never shows. During event scripts (cutscenes) and with the
 * editor's free camera the game's own test decides, as with
 * gameplay_walls 0. The ray-traced scene already holds every group. */
#define _POSIX_C_SOURCE 200809L
#include "gameplay_walls.h"
#include "dev_state.h"
#include "room_editor.h"
#include "scene_capture.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned int CFC2(int reg);
extern int pc_gte_widescreen;
extern int pc_room_group_black(const void* data, unsigned index) __attribute__((weak));
extern const void* pc_scene_room_data(void) __attribute__((weak));
/* scene_submit.inc (battle builds): an event script runs; the battle
 * camera's target (weapon drawn, a target chosen). */
extern int pc_battle_event_running(void) __attribute__((weak));
extern int pc_battle_camera_target(int position[3]) __attribute__((weak));

int pc_gameplay_walls = 0; /* 0 original, 1 smart */
int pc_gameplay_walls_ghost = 0; /* occluding groups: 0 left out, 1 ghosts */
float pc_gameplay_walls_opacity = 0.3f; /* a ghost's share of pixels kept */
float pc_gameplay_walls_radius = 64; /* world units around Ashley's body */
int pc_gameplay_walls_fade_out = 4; /* game frames to fade out */
int pc_gameplay_walls_fade_in = 7; /* game frames to fade in */
int pc_gameplay_walls_clear = 10; /* game frames clear before a group stops occluding */

typedef struct {
    short m[3][3];
    short pad;
    int t[3];
} gte_matrix;

#define MAX_GROUPS 4096
#define GRID_W 80
#define GRID_H 60
#define CELL 4.0f
#define NEAR_Z 16.0f
#define COVER_CELLS 6 /* cells seen in front of the play area that make a group occlude */
#define FRONT_MARGIN 96 /* world units nearer than the focus: in front of the play area */

typedef struct {
    float shown, previous; /* drawn share (1 solid, 0 left out), last frame's */
    unsigned char occluding, candidate, seen;
    unsigned short clear; /* frames in a row clear of the wide region */
    /* Diagnostics (RPC room_groups): this frame's counts. */
    unsigned short focus_cells, cover_cells, void_cells, cells;
} group_state;

static const void* state_data;
static unsigned state_count, state_signature;
static int state_fresh; /* the first frame in this room: no fades */
static group_state states[MAX_GROUPS];
static int active_frame; /* the latest frame used smart decisions */

static float base_depth[GRID_W * GRID_H];
static unsigned short stamp[GRID_W * GRID_H];

static unsigned block_signature(const void* data, unsigned count)
{
    const unsigned* w = (const unsigned*)data;
    unsigned h = 2166136261u;
    for (unsigned i = 0; i < count; ++i)
        h = (h ^ w[1 + i * 16 + 7]) * 16777619u;
    return h ^ count;
}

static int is_kept(unsigned flags, unsigned facing, unsigned mode)
{
    if (flags & 0x1100 || ((flags >> 15) & mode & 1))
        return -1; /* never drawn (room flags, the other set) */
    return (facing >> 16) || !((flags ^ 255) & facing & 255);
}

/* ---- the coarse rasteriser ---- */

typedef struct {
    float x, y, z; /* view space */
} vec3f;

typedef struct {
    float r[3][3], t[3];
    float hx, h, ox, oy; /* projection; screen origin of the grid */
} view_t;

static vec3f to_view(const view_t* v, const float w[3])
{
    vec3f o;
    o.x = v->r[0][0] * w[0] + v->r[0][1] * w[1] + v->r[0][2] * w[2] + v->t[0];
    o.y = v->r[1][0] * w[0] + v->r[1][1] * w[1] + v->r[1][2] * w[2] + v->t[1];
    o.z = v->r[2][0] * w[0] + v->r[2][1] * w[1] + v->r[2][2] * w[2] + v->t[2];
    return o;
}
/* Grid coordinates (cells) and view depth. */
typedef struct {
    float gx, gy, iz; /* 1 / z */
} gvert;
static gvert project(const view_t* v, vec3f p)
{
    gvert g;
    g.gx = (v->hx * p.x / p.z - v->ox) / CELL;
    g.gy = (v->h * p.y / p.z - v->oy) / CELL;
    g.iz = 1.0f / p.z;
    return g;
}

/* What one rasterised triangle does with each covered cell. */
typedef struct {
    int pass; /* 0: base depth, 1: a candidate group */
    int group;
    /* the focus region (cells): capsule a-b with radius r, focus depth */
    float ax, ay, bx, by, r, r_wide, focus_z, front_z;
    int has_target;
    float tx, ty, tr, tr_wide, target_z;
    group_state* g;
    int focus_hit, focus_wide_hit;
} raster_job;

static float segment_distance(float px, float py, float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy;
    float t = l > 1e-6f ? ((px - ax) * dx + (py - ay) * dy) / l : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    float qx = ax + dx * t - px, qy = ay + dy * t - py;
    return sqrtf(qx * qx + qy * qy);
}

static void cell(raster_job* j, int x, int y, float z)
{
    int i = y * GRID_W + x;
    if (j->pass == 0) {
        if (z < base_depth[i])
            base_depth[i] = z;
        return;
    }
    float cx = x + 0.5f, cy = y + 0.5f;
    group_state* g = j->g;
    /* The focus region, nearer than the focus. */
    float d = segment_distance(cx, cy, j->ax, j->ay, j->bx, j->by);
    if (z < j->focus_z) {
        if (d <= j->r) {
            j->focus_hit = 1;
            if (g->focus_cells < 65535)
                g->focus_cells++;
        }
        if (d <= j->r_wide)
            j->focus_wide_hit = 1;
    }
    if (j->has_target && z < j->target_z) {
        float td = hypotf(cx - j->tx, cy - j->ty);
        if (td <= j->tr)
            j->focus_hit = 1;
        if (td <= j->tr_wide)
            j->focus_wide_hit = 1;
    }
    if (stamp[i] == (unsigned short)(j->group + 1))
        return; /* counted for this group already */
    stamp[i] = (unsigned short)(j->group + 1);
    if (g->cells < 65535)
        g->cells++;
    if (base_depth[i] >= 1e30f && g->void_cells < 65535)
        g->void_cells++;
    /* Seen (in front of the original picture, or over void) well in front
     * of the focus: between the camera and the play area. */
    if (z < base_depth[i] - 4 && z < j->front_z && g->cover_cells < 65535)
        g->cover_cells++;
}

static void raster_triangle(raster_job* j, gvert a, gvert b, gvert c)
{
    float minx = fminf(a.gx, fminf(b.gx, c.gx)), maxx = fmaxf(a.gx, fmaxf(b.gx, c.gx));
    float miny = fminf(a.gy, fminf(b.gy, c.gy)), maxy = fmaxf(a.gy, fmaxf(b.gy, c.gy));
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x1 < 0 || y1 < 0 || x0 >= GRID_W || y0 >= GRID_H)
        return;
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > GRID_W - 1 ? GRID_W - 1 : x1, y1 = y1 > GRID_H - 1 ? GRID_H - 1 : y1;
    float area = (b.gx - a.gx) * (c.gy - a.gy) - (c.gx - a.gx) * (b.gy - a.gy);
    if (fabsf(area) < 1e-6f)
        return;
    float inv = 1.0f / area;
    int any = 0;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((b.gx - px) * (c.gy - py) - (c.gx - px) * (b.gy - py)) * inv;
            float w1 = ((c.gx - px) * (a.gy - py) - (a.gx - px) * (c.gy - py)) * inv;
            float w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            any = 1;
            cell(j, x, y, 1.0f / (w0 * a.iz + w1 * b.iz + w2 * c.iz));
        }
    if (!any) {
        /* Thinner than a cell (a far beam, a wall seen edge on): its
         * centre's cell, so it still counts. */
        float cx = (a.gx + b.gx + c.gx) / 3, cy = (a.gy + b.gy + c.gy) / 3;
        int x = (int)floorf(cx), y = (int)floorf(cy);
        if (x >= 0 && y >= 0 && x < GRID_W && y < GRID_H)
            cell(j, x, y, 3.0f / (a.iz + b.iz + c.iz));
    }
}

/* A view-space triangle, clipped at the near plane, rasterised. */
static void raster(raster_job* j, const view_t* v, vec3f p0, vec3f p1, vec3f p2)
{
    vec3f in[3] = { p0, p1, p2 }, out[4];
    int n = 0;
    for (int i = 0; i < 3; i++) {
        vec3f a = in[i], b = in[(i + 1) % 3];
        int ain = a.z >= NEAR_Z, bin = b.z >= NEAR_Z;
        if (ain)
            out[n++] = a;
        if (ain != bin) {
            float t = (NEAR_Z - a.z) / (b.z - a.z);
            vec3f q = { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NEAR_Z };
            out[n++] = q;
        }
    }
    if (n < 3)
        return;
    gvert g[4];
    for (int i = 0; i < n; i++)
        g[i] = project(v, out[i]);
    raster_triangle(j, g[0], g[1], g[2]);
    if (n == 4)
        raster_triangle(j, g[0], g[2], g[3]);
}

/* ---- the frame ---- */

static int state_block(const void* data)
{
    unsigned count = *(const unsigned*)data;
    if (!count || count > MAX_GROUPS)
        return 0;
    unsigned sig = block_signature(data, count);
    if (data != state_data || count != state_count || sig != state_signature) {
        state_data = data;
        state_count = count;
        state_signature = sig;
        memset(states, 0, sizeof states);
        state_fresh = 1;
    }
    return 1;
}

static int game_camera(view_t* v)
{
    const gte_matrix* view = (const gte_matrix*)(uintptr_t)0x1f800014;
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++)
            v->r[a][b] = view->m[a][b] / 4096.0f;
        v->t[a] = (float)view->t[a];
    }
    v->h = (float)(CFC2(26) & 0xffff);
    v->hx = v->h * (pc_gte_widescreen ? 0.75f : 1.0f);
    /* The grid's origin relative to the projection centre: the game's
     * frames are 320 x 240 around it (widescreen narrows hx instead). */
    v->ox = -160, v->oy = -120;
    return v->h > 0;
}

int pc_gameplay_walls_frame(const void* data, unsigned facing, unsigned mode)
{
    active_frame = 0;
    if (!pc_gameplay_walls || !data) {
        state_fresh = 1; /* off, or the free camera: no fades when smart walls decide again */
        return 0;
    }
    if (!state_block(data))
        return 0;
    if (pc_battle_event_running && pc_battle_event_running()) {
        state_fresh = 1; /* the game's own test; no fade when play resumes */
        return 0;
    }
    struct pc_dev_state s;
    memset(&s, 0, sizeof s);
    if (!pc_dev_state)
        return 0;
    pc_dev_state(&s);
    if (!s.valid || !s.has_player)
        return 0;
    int tri_count;
    const scene_triangle* tris = pc_scene_triangle_data(&tri_count);
    if (!tris || !tri_count || !pc_scene_room_data || pc_scene_room_data() != data)
        return 0;
    view_t v;
    if (!game_camera(&v))
        return 0;
    unsigned count = state_count;

    /* Which groups the game keeps, which are candidates. */
    for (unsigned i = 0; i < count; i++) {
        unsigned flags = *(const unsigned*)((const unsigned char*)data + 4 + i * 0x40);
        int kept = is_kept(flags, facing, mode);
        group_state* g = &states[i];
        /* Not the black shells, and not camera-facing planes (flag 0x400:
         * not in the captured scene, so never tested). */
        g->candidate = kept == 0 && !(flags & 0x400) && !(pc_room_group_black && pc_room_group_black(data, i));
        g->focus_cells = g->cover_cells = g->void_cells = g->cells = 0;
    }

    /* The focus: Ashley's body (feet to head), in grid cells. */
    float feet[3] = { (float)s.x, (float)s.y - 8, (float)s.z }, head[3] = { (float)s.x, (float)s.y - 150, (float)s.z };
    vec3f vf = to_view(&v, feet), vh = to_view(&v, head);
    if (vf.z < NEAR_Z || vh.z < NEAR_Z)
        return 0;
    raster_job job;
    memset(&job, 0, sizeof job);
    gvert gf = project(&v, vf), gh = project(&v, vh);
    float focus_z = (vf.z + vh.z) * 0.5f;
    float radius = pc_gameplay_walls_radius > 0 ? pc_gameplay_walls_radius : 0;
    job.ax = gf.gx, job.ay = gf.gy, job.bx = gh.gx, job.by = gh.gy;
    job.r = (v.h * (radius + 32) / focus_z) / CELL; /* body half width 32 */
    job.r_wide = job.r * 1.3f;
    job.focus_z = focus_z - 24;
    job.front_z = focus_z - FRONT_MARGIN;
    int target[3];
    if (pc_battle_camera_target && pc_battle_camera_target(target)) {
        float tw[3] = { (float)target[0], (float)target[1], (float)target[2] };
        vec3f vt = to_view(&v, tw);
        if (vt.z > NEAR_Z) {
            gvert gt = project(&v, vt);
            job.has_target = 1;
            job.tx = gt.gx, job.ty = gt.gy;
            job.tr = (v.h * (radius + 32) / vt.z) / CELL;
            job.tr_wide = job.tr * 1.3f;
            job.target_z = vt.z - 24;
        }
    }

    for (int i = 0; i < GRID_W * GRID_H; i++)
        base_depth[i] = 1e30f, stamp[i] = 0;
    for (int pass = 0; pass < 2; pass++) {
        job.pass = pass;
        for (int t = 0; t < tri_count; t++) {
            const scene_triangle* tri = &tris[t];
            if (tri->group >= count || (tri->code & 2))
                continue;
            group_state* g = &states[tri->group];
            unsigned flags = *(const unsigned*)((const unsigned char*)data + 4 + tri->group * 0x40);
            if (pass == 0 ? is_kept(flags, facing, mode) != 1 : !g->candidate)
                continue;
            vec3f p[3];
            for (int k = 0; k < 3; k++)
                p[k] = to_view(&v, tri->vertex[k].position);
            const unsigned* poly = (const unsigned*)((const unsigned char*)data + tri->polygon);
            int twosided = (poly[3] & 0x01000000) != 0;
            if (!twosided) {
                /* A front face as the PS1 culls (NCLIP on the projected
                 * corners): ((b - a) x (c - a)) . a > 0 in view space. */
                float e1x = p[1].x - p[0].x, e1y = p[1].y - p[0].y, e1z = p[1].z - p[0].z;
                float e2x = p[2].x - p[0].x, e2y = p[2].y - p[0].y, e2z = p[2].z - p[0].z;
                float nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
                if (nx * p[0].x + ny * p[0].y + nz * p[0].z <= 0)
                    continue;
            }
            job.group = tri->group;
            job.g = g;
            job.focus_hit = job.focus_wide_hit = 0;
            raster(&job, &v, p[0], p[1], p[2]);
            if (pass == 1) {
                if (job.focus_hit)
                    g->seen |= 2; /* occludes now */
                if (job.focus_wide_hit)
                    g->seen |= 4; /* near the wide region */
            }
        }
    }

    /* Decisions, hysteresis and fades (gameplay_walls_clear, _fade_in, _fade_out: at least a frame). */
    unsigned short clear_frames = (unsigned short)(pc_gameplay_walls_clear < 1 ? 1
            : pc_gameplay_walls_clear > 600                                 ? 600
                                                                            : pc_gameplay_walls_clear);
    int in_frames = pc_gameplay_walls_fade_in < 1 ? 1 : pc_gameplay_walls_fade_in;
    int out_frames = pc_gameplay_walls_fade_out < 1 ? 1 : pc_gameplay_walls_fade_out;
    for (unsigned i = 0; i < count; i++) {
        group_state* g = &states[i];
        int now = (g->seen & 2) || g->cover_cells >= COVER_CELLS;
        int near_ = (g->seen & 4) || g->cover_cells >= COVER_CELLS / 2;
        g->seen = 0;
        if (!g->candidate) {
            /* Kept by the game (or never drawn): solid once faded in. */
            g->occluding = 0;
            g->clear = clear_frames;
        } else if (now) {
            g->occluding = 1;
            g->clear = 0;
        } else if (near_) {
            g->clear = 0;
        } else if (g->clear < clear_frames ? ++g->clear >= clear_frames : g->occluding) {
            g->occluding = 0; /* (or the setting went below its count) */
        } else if (state_fresh) {
            g->occluding = 0;
        }
        float target = g->occluding ? 0.0f : 1.0f;
        g->previous = g->shown;
        if (state_fresh)
            g->shown = g->previous = target;
        else if (g->shown < target)
            g->shown = fminf(target, g->shown + 1.0f / in_frames);
        else
            g->shown = fmaxf(target, g->shown - 1.0f / out_frames);
    }
    state_fresh = 0;
    active_frame = 1;
    return 1;
}

/* Group `index` the current frame tests: whether to draw it (1) and how
 * (cut: share of its pixels left out now and in the frame before; ghost:
 * keep edges, mark as a ghost). Groups the game's own test keeps always
 * draw, fading in after a smart hide. */
int pc_gameplay_walls_group(unsigned index, int kept, float* cut, float* cut_previous, int* ghost)
{
    *cut = *cut_previous = 0;
    *ghost = 0;
    if (!active_frame || index >= state_count)
        return kept;
    const group_state* g = &states[index];
    if (kept) {
        /* Kept by the game: solid, or fading in after a smart hide. */
        *cut = 1 - g->shown;
        *cut_previous = 1 - g->previous;
        return 1;
    }
    if (!g->candidate)
        return 0;
    float keep = pc_gameplay_walls_opacity < 0 ? 0 : pc_gameplay_walls_opacity > 1 ? 1 : pc_gameplay_walls_opacity;
    if (pc_gameplay_walls_ghost) {
        *cut = (1 - g->shown) * (1 - keep);
        *cut_previous = (1 - g->previous) * (1 - keep);
        *ghost = g->shown < 1 || g->previous < 1;
        return 1;
    }
    if (g->shown <= 0 && g->previous <= 0)
        return 0;
    *cut = 1 - g->shown;
    *cut_previous = 1 - g->previous;
    return 1;
}

int pc_gameplay_walls_info(unsigned index, float* shown, int* occluding, int counts[4])
{
    if (!pc_gameplay_walls || index >= state_count || !state_data)
        return 0;
    const group_state* g = &states[index];
    *shown = g->shown;
    *occluding = g->occluding;
    counts[0] = g->focus_cells, counts[1] = g->cover_cells, counts[2] = g->void_cells, counts[3] = g->cells;
    return active_frame ? 1 : 2;
}
