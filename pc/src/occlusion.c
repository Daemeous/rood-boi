/* Occlusion in play (occlusion.h; Native_Rendering.md "Occlusion").
 *
 * Once per game frame (func_8009723C, the current room) this decides who
 * matters and whether each of them is hidden:
 *
 * - subjects: Ashley; in battle (BATTLE's action state, as the depth of
 *   field's camera subject reads it: pc_dev_battle_camera/_action, the
 *   same state post_profile.cpp pick_subject decides from) the targeting
 *   sphere's candidate, the running action's performer and first target,
 *   and the enemies within occlusion_enemy_range of Ashley, nearest first;
 *   at most PC_OCCLUSION_SUBJECTS, Ashley first (pc_occlusion_subjects
 *   picks the kinds);
 * - hidden: five rays from the eye (the game camera) to the subject's
 *   middle, head, knees and sides against the room triangles drawn this
 *   frame (front faces as the PS1 culls, opaque; the subject's own last
 *   stretch skipped), as the editor's cutaway asks (editor_view.cpp
 *   focus_hidden); any ray blocked counts, and a subject stays hidden for
 *   occlusion_hold seconds after its rays clear (no flicker at an edge);
 * - the fade (and the silhouettes' strength) eases toward hidden over
 *   occlusion_ease seconds of game time (vsyncs), smoothstepped, so a wall
 *   never pops; a subject that stops mattering eases out the same way.
 *
 * render.c draws from it: per list and frame-interpolation step the
 * subjects in the camera's view space; per instance whether it is a
 * character (never faded; a subject's own meshes get its silhouette). */
#include "occlusion.h"
#include "dev_state.h"
#include "render.h"
#include "scene_capture.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pc_occlusion = PC_OCCLUSION_FADE;
float pc_occlusion_visibility = 0.35f;
float pc_occlusion_radius = 96;
float pc_occlusion_aspect = 1.3f;
float pc_occlusion_softness = 0.45f;
float pc_occlusion_ease = 0.2f;
float pc_occlusion_hold = 0.25f;
int pc_occlusion_dither = 0;
int pc_occlusion_subjects = 1 | 2 | 4 | 8;
float pc_occlusion_enemy_range = 640;
int pc_occlusion_props = 1;
int pc_occlusion_cutscenes = 0;
float pc_occlusion_silhouette_ashley = 0.3f;
float pc_occlusion_silhouette_enemy = 0.45f;
float pc_occlusion_silhouette_rim = 0.6f;
float pc_occlusion_tint_ashley[3] = { 0.42f, 0.50f, 0.60f }; /* cool neutral, a little lighter than the stone */
float pc_occlusion_tint_enemy[3] = { 0.58f, 0.26f, 0.22f }; /* muted rust */
const char* const pc_occlusion_names[4] = { "off", "fade", "fade_silhouette", "silhouette" };

extern unsigned pc_vsync_count(void) __attribute__((weak));
extern int pc_battle_event_running(void) __attribute__((weak));
extern int pc_room_drawn_groups(unsigned char* out, int max, unsigned* facing) __attribute__((weak));

#define SLOTS 17
#define HEIGHT 150.0f /* Ashley's height, world units (gameplay_walls.c: feet to head) */
#define BODY 28.0f /* her body's half width */

typedef struct {
    int wanted; /* a subject this frame */
    int seen; /* in the room this frame */
    float feet[3], radius;
    float center[3], previous[3];
    int has_previous;
    float fade_linear, hidden_linear; /* eased, linear */
    float fade, fade_previous, hidden;
    float hold; /* seconds left counting as hidden */
    int blocked;
    float distance; /* from Ashley (ordering) */
} slot_state;

static slot_state slots[SLOTS];
static pc_occlusion_subject subjects[PC_OCCLUSION_SUBJECTS];
static int subject_count;
static uint32_t updated_serial;
static int updated;
static unsigned last_vsync;
static int have_vsync, scene_serial = -1;
static float ashley_radius = 0;

/* ---- rays against the drawn room ---- */
typedef struct {
    float a[3], e1[3], e2[3], lo[3], hi[3];
    uint16_t group;
    uint32_t flags;
    uint8_t opaque;
} ray_triangle;
static ray_triangle* tris;
static int tri_count, tri_capacity, tri_serial = -1;
static unsigned char drawn[4096];
static int drawn_count;

static void cache_triangles(void)
{
    int count = 0;
    const scene_triangle* t = pc_scene_triangle_data(&count);
    if (pc_scene_serial() == tri_serial && count == tri_count)
        return;
    tri_serial = pc_scene_serial();
    if (count > tri_capacity) {
        ray_triangle* grown = realloc(tris, (size_t)count * sizeof *tris);
        if (!grown) {
            tri_count = 0;
            return;
        }
        tris = grown;
        tri_capacity = count;
    }
    tri_count = t ? count : 0;
    for (int i = 0; i < tri_count; i++) {
        ray_triangle* c = &tris[i];
        const float *a = t[i].vertex[0].position, *b = t[i].vertex[1].position, *d = t[i].vertex[2].position;
        for (int k = 0; k < 3; k++) {
            c->a[k] = a[k], c->e1[k] = b[k] - a[k], c->e2[k] = d[k] - a[k];
            c->lo[k] = fminf(a[k], fminf(b[k], d[k]));
            c->hi[k] = fmaxf(a[k], fmaxf(b[k], d[k]));
        }
        c->group = t[i].group;
        c->flags = t[i].flags;
        c->opaque = !(t[i].code & 2);
    }
}

/* How many of the rays from `eye` to `ends` are blocked before the last
 * `skip` units. */
static int blocked_rays(const float eye[3], float ends[][3], int rays, float skip, float facing_sign)
{
    float dir[8][3], len[8], lo[3], hi[3];
    int blocked[8] = { 0 }, count = 0;
    for (int k = 0; k < 3; k++)
        lo[k] = hi[k] = eye[k];
    for (int r = 0; r < rays; r++) {
        float l = 0;
        for (int k = 0; k < 3; k++) {
            dir[r][k] = ends[r][k] - eye[k];
            l += dir[r][k] * dir[r][k];
            lo[k] = fminf(lo[k], ends[r][k]), hi[k] = fmaxf(hi[k], ends[r][k]);
        }
        len[r] = sqrtf(l);
        if (len[r] < 1e-3f)
            return 0;
        for (int k = 0; k < 3; k++)
            dir[r][k] /= len[r];
    }
    for (int t = 0; t < tri_count && count < rays; t++) {
        const ray_triangle* c = &tris[t];
        if (!c->opaque || (c->flags & 0x1100) || c->hi[0] < lo[0] || c->lo[0] > hi[0] || c->hi[1] < lo[1]
            || c->lo[1] > hi[1] || c->hi[2] < lo[2] || c->lo[2] > hi[2])
            continue;
        if (c->group >= drawn_count || !drawn[c->group])
            continue; /* not drawn this frame (the game's cutaway, the other set) */
        float n[3] = { c->e1[1] * c->e2[2] - c->e1[2] * c->e2[1], c->e1[2] * c->e2[0] - c->e1[0] * c->e2[2],
            c->e1[0] * c->e2[1] - c->e1[1] * c->e2[0] };
        float to[3] = { c->a[0] - eye[0], c->a[1] - eye[1], c->a[2] - eye[2] };
        if (facing_sign * (n[0] * to[0] + n[1] * to[1] + n[2] * to[2]) <= 0)
            continue; /* a back face: the PS1 does not draw it */
        for (int r = 0; r < rays; r++) {
            if (blocked[r])
                continue;
            const float* v = dir[r];
            float p[3] = { v[1] * c->e2[2] - v[2] * c->e2[1], v[2] * c->e2[0] - v[0] * c->e2[2],
                v[0] * c->e2[1] - v[1] * c->e2[0] };
            float det = c->e1[0] * p[0] + c->e1[1] * p[1] + c->e1[2] * p[2];
            if (fabsf(det) < 1e-8f)
                continue;
            float inv = 1 / det, tv[3] = { eye[0] - c->a[0], eye[1] - c->a[1], eye[2] - c->a[2] };
            float u = (tv[0] * p[0] + tv[1] * p[1] + tv[2] * p[2]) * inv;
            if (u < 0 || u > 1)
                continue;
            float q[3] = { tv[1] * c->e1[2] - tv[2] * c->e1[1], tv[2] * c->e1[0] - tv[0] * c->e1[2],
                tv[0] * c->e1[1] - tv[1] * c->e1[0] };
            float w = (v[0] * q[0] + v[1] * q[1] + v[2] * q[2]) * inv;
            if (w < 0 || u + w > 1)
                continue;
            float hit = (c->e2[0] * q[0] + c->e2[1] * q[1] + c->e2[2] * q[2]) * inv;
            if (hit > 16 && hit < len[r] - skip) {
                blocked[r] = 1;
                ++count;
            }
        }
    }
    return count;
}

/* ---- the frame ---- */
static float scale_of(const slot_state* s, int slot)
{
    if (slot == 0 || ashley_radius <= 0 || s->radius <= 0)
        return 1;
    float k = s->radius / ashley_radius;
    return k < 0.75f ? 0.75f : k > 3 ? 3 : k;
}

static void want(int slot)
{
    if (slot >= 0 && slot < SLOTS && slots[slot].seen)
        slots[slot].wanted = 1;
}

static float smooth01(float x)
{
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    return x * x * (3 - 2 * x);
}

static void clear_all(void)
{
    memset(slots, 0, sizeof slots);
    subject_count = 0;
}

void pc_occlusion_frame(int free_camera)
{
    updated = 0;
    if (free_camera || (!pc_occlusion_cutscenes && pc_battle_event_running && pc_battle_event_running())) {
        /* The editor's camera or a cutscene: nothing fades; play eases back in. */
        clear_all();
        return;
    }
    if (pc_scene_serial() != scene_serial) {
        scene_serial = pc_scene_serial();
        clear_all(); /* another room: no fades carried over */
    }
    /* Game time since the last frame (vsyncs at 59.94 Hz). */
    float dt = 1.0f / 30;
    if (pc_vsync_count) {
        unsigned v = pc_vsync_count();
        dt = have_vsync ? (float)(v - last_vsync) / 59.94f : 0;
        last_vsync = v, have_vsync = 1;
    }
    dt = dt < 0 ? 0 : dt > 0.25f ? 0.25f : dt;

    struct pc_dev_actor actors[SLOTS];
    int n = pc_dev_actors ? pc_dev_actors(actors, SLOTS) : 0;
    int ashley_weapon = 0;
    for (int i = 0; i < SLOTS; i++)
        slots[i].seen = slots[i].wanted = 0;
    for (int i = 0; i < n; i++) {
        int k = actors[i].slot;
        if (k < 0 || k >= SLOTS)
            continue;
        slot_state* s = &slots[k];
        s->seen = 1;
        memcpy(s->feet, actors[i].pos, sizeof s->feet);
        s->radius = actors[i].radius;
        if (k == 0) {
            ashley_radius = actors[i].radius;
            ashley_weapon = actors[i].weapon_drawn;
        }
    }
    int mode = pc_occlusion;
    if (mode != PC_OCCLUSION_OFF) {
        if (pc_occlusion_subjects & 1)
            want(0);
        struct pc_dev_camera cam;
        struct pc_dev_action act;
        memset(&cam, 0, sizeof cam);
        memset(&act, 0, sizeof act);
        /* Battle: Ashley's weapon drawn, or BATTLE's action state (targeting, an action). */
        int action_state = pc_dev_battle_camera && pc_dev_battle_camera(&cam) && cam.state == 2;
        int battle = action_state || ashley_weapon;
        if (action_state) {
            if ((pc_occlusion_subjects & 2) && cam.candidate)
                want(cam.candidate_actor);
            if ((pc_occlusion_subjects & 4) && pc_dev_battle_action && pc_dev_battle_action(&act)) {
                want(act.actor);
                want(act.target);
            }
        }
        for (int k = 1; k < SLOTS; k++) {
            slot_state* s = &slots[k];
            if (!s->seen || !slots[0].seen)
                continue;
            float dx = s->feet[0] - slots[0].feet[0], dz = s->feet[2] - slots[0].feet[2];
            s->distance = sqrtf(dx * dx + dz * dz);
            if (battle && (pc_occlusion_subjects & 8) && s->distance <= pc_occlusion_enemy_range)
                s->wanted = 1;
        }
    }

    /* The eye and the room's drawn triangles. */
    float r[3][3], t[3], h, ox, oy, inverse[3][3], eye[3] = { 0, 0, 0 }, side[3] = { 1, 0, 0 };
    int have_eye = pc_scene_game_camera(r, t, &h, &ox, &oy) && pc_scene_game_inverse(inverse);
    float facing_sign = 1;
    if (have_eye) {
        for (int a = 0; a < 3; a++)
            eye[a] = -(inverse[a][0] * t[0] + inverse[a][1] * t[1] + inverse[a][2] * t[2]);
        float l = sqrtf(inverse[0][0] * inverse[0][0] + inverse[1][0] * inverse[1][0] + inverse[2][0] * inverse[2][0]);
        if (l > 1e-6f)
            side[0] = inverse[0][0] / l, side[1] = inverse[1][0] / l, side[2] = inverse[2][0] / l;
        float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0])
            + r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
        facing_sign = det < 0 ? -1.0f : 1.0f;
        cache_triangles();
        unsigned facing = 0;
        drawn_count = pc_room_drawn_groups ? pc_room_drawn_groups(drawn, 4096, &facing) : 0;
    }

    int fades = mode == PC_OCCLUSION_FADE || mode == PC_OCCLUSION_FADE_SILHOUETTE;
    int silhouettes = mode == PC_OCCLUSION_FADE_SILHOUETTE || mode == PC_OCCLUSION_SILHOUETTE;
    float ease = pc_occlusion_ease > 0.001f ? pc_occlusion_ease : 0.001f;
    for (int k = 0; k < SLOTS; k++) {
        slot_state* s = &slots[k];
        float scale = scale_of(s, k);
        float height = HEIGHT * scale, body = BODY * scale;
        float c[3] = { s->feet[0], s->feet[1] - height * 0.5f, s->feet[2] };
        if (s->seen) {
            if (s->has_previous) {
                memcpy(s->previous, s->center, sizeof s->previous);
                /* A jump (a warp, a new room position): no slide. */
                float dx = c[0] - s->previous[0], dy = c[1] - s->previous[1], dz = c[2] - s->previous[2];
                if (dx * dx + dy * dy + dz * dz > 256.0f * 256.0f)
                    memcpy(s->previous, c, sizeof s->previous);
            } else
                memcpy(s->previous, c, sizeof s->previous);
            memcpy(s->center, c, sizeof s->center);
            s->has_previous = 1;
        } else
            s->has_previous = 0;
        int hidden_now = 0;
        s->blocked = 0;
        if (s->wanted && have_eye && tri_count) {
            float ends[5][3];
            for (int a = 0; a < 3; a++) {
                ends[0][a] = c[a];
                ends[1][a] = s->feet[a];
                ends[2][a] = s->feet[a];
                ends[3][a] = c[a] + side[a] * body;
                ends[4][a] = c[a] - side[a] * body;
            }
            ends[1][1] = s->feet[1] - height * 0.9f; /* the head */
            ends[2][1] = s->feet[1] - height * 0.25f; /* the knees */
            s->blocked = blocked_rays(eye, ends, 5, fmaxf(body * 1.5f, 24.0f), facing_sign);
            hidden_now = s->blocked > 0;
        }
        if (hidden_now)
            s->hold = pc_occlusion_hold > 0 ? pc_occlusion_hold : 0;
        else
            s->hold = fmaxf(0, s->hold - dt);
        int hidden = s->wanted && (hidden_now || s->hold > 0);
        float step = dt / ease;
        float fade_target = hidden && fades ? 1.0f : 0.0f, hide_target = hidden && silhouettes ? 1.0f : 0.0f;
        s->fade_previous = s->fade;
        s->fade_linear += fmaxf(-step, fminf(step, fade_target - s->fade_linear));
        s->hidden_linear += fmaxf(-step, fminf(step, hide_target - s->hidden_linear));
        if (!s->seen)
            s->fade_linear = s->hidden_linear = 0;
        s->fade = smooth01(s->fade_linear);
        s->hidden = smooth01(s->hidden_linear);
    }

    /* The subjects drawn: Ashley first, then the wanted ones nearest her,
     * then the ones still easing out. */
    subject_count = 0;
    for (int pass = 0; pass < 3 && subject_count < PC_OCCLUSION_SUBJECTS; pass++) {
        for (;;) {
            int best = -1;
            for (int k = 0; k < SLOTS; k++) {
                slot_state* s = &slots[k];
                int taken = 0;
                for (int i = 0; i < subject_count; i++)
                    taken |= subjects[i].slot == k;
                if (taken || !s->seen)
                    continue;
                int ok = pass == 0 ? k == 0 && s->wanted
                    : pass == 1    ? s->wanted
                                   : s->fade > 0 || s->fade_previous > 0 || s->hidden > 0;
                if (ok && (best < 0 || s->distance < slots[best].distance))
                    best = k;
            }
            if (best < 0 || subject_count >= PC_OCCLUSION_SUBJECTS)
                break;
            slot_state* s = &slots[best];
            pc_occlusion_subject* o = &subjects[subject_count++];
            float scale = scale_of(s, best);
            o->slot = best;
            o->enemy = best != 0;
            memcpy(o->center, s->center, sizeof o->center);
            memcpy(o->previous, s->previous, sizeof o->previous);
            o->half_w = (pc_occlusion_radius > 0 ? pc_occlusion_radius : 0) * scale;
            o->half_h = o->half_w * (pc_occlusion_aspect > 0.1f ? pc_occlusion_aspect : 0.1f);
            o->body = BODY * scale;
            o->feet_y = s->feet[1];
            o->fade = s->fade, o->fade_previous = s->fade_previous;
            o->hidden = s->hidden;
            o->rays_blocked = s->blocked;
            if (pass == 0)
                break;
        }
    }
    updated = 1;
    updated_serial = render_frame_serial();
}

int pc_occlusion_subjects_get(const pc_occlusion_subject** out)
{
    *out = subjects;
    /* Stale (no room drawn for a while: a menu, a load): none. */
    if (!updated || (uint32_t)(render_frame_serial() - updated_serial) > 2)
        return 0;
    return subject_count;
}

int pc_occlusion_character(const float p[3], int* subject)
{
    *subject = -1;
    int found = -1;
    for (int k = 0; k < SLOTS; k++) {
        const slot_state* s = &slots[k];
        if (!s->seen)
            continue;
        float scale = scale_of(s, k);
        float dx = p[0] - s->feet[0], dz = p[2] - s->feet[2];
        float reach = fmaxf(s->radius * 1.5f, 64.0f * scale) + 32;
        if (dx * dx + dz * dz > reach * reach || p[1] < s->feet[1] - HEIGHT * 1.8f * scale || p[1] > s->feet[1] + 48)
            continue;
        if (found < 0 || k == 0)
            found = k;
    }
    if (found < 0)
        return 0;
    for (int i = 0; i < subject_count; i++)
        if (subjects[i].slot == found)
            *subject = i;
    return 1;
}

/* ---- settings (settings file, RPC occlusion, the dev panel) ---- */
typedef struct {
    const char* key;
    int* i;
    float* f; /* `count` floats */
    int count;
    float lo, hi;
} tunable;
static const tunable tunables[] = {
    { "occlusion_visibility", NULL, &pc_occlusion_visibility, 1, 0, 1 },
    { "occlusion_radius", NULL, &pc_occlusion_radius, 1, 0, 1024 },
    { "occlusion_aspect", NULL, &pc_occlusion_aspect, 1, 0.1f, 4 },
    { "occlusion_softness", NULL, &pc_occlusion_softness, 1, 0.01f, 1 },
    { "occlusion_ease", NULL, &pc_occlusion_ease, 1, 0, 5 },
    { "occlusion_hold", NULL, &pc_occlusion_hold, 1, 0, 5 },
    { "occlusion_dither", &pc_occlusion_dither, NULL, 0, 0, 1 },
    { "occlusion_subjects", &pc_occlusion_subjects, NULL, 0, 0, 15 },
    { "occlusion_enemy_range", NULL, &pc_occlusion_enemy_range, 1, 0, 8192 },
    { "occlusion_props", &pc_occlusion_props, NULL, 0, 0, 1 },
    { "occlusion_cutscenes", &pc_occlusion_cutscenes, NULL, 0, 0, 1 },
    { "occlusion_silhouette_ashley", NULL, &pc_occlusion_silhouette_ashley, 1, 0, 1 },
    { "occlusion_silhouette_enemy", NULL, &pc_occlusion_silhouette_enemy, 1, 0, 1 },
    { "occlusion_silhouette_rim", NULL, &pc_occlusion_silhouette_rim, 1, 0, 1 },
    { "occlusion_tint_ashley", NULL, pc_occlusion_tint_ashley, 3, 0, 1 },
    { "occlusion_tint_enemy", NULL, pc_occlusion_tint_enemy, 3, 0, 1 },
};
#define TUNABLES ((int)(sizeof tunables / sizeof tunables[0]))

static int mode_from(const char* text)
{
    char word[32] = "";
    int n = 0;
    while (*text == ' ' || *text == '\t' || *text == '"')
        text++;
    while (*text && *text != '"' && *text != ' ' && *text != '\t' && *text != '\r' && *text != '\n' && *text != ','
        && *text != '}' && n < 31)
        word[n++] = *text++;
    word[n] = 0;
    for (int k = 0; k < 4; k++)
        if (!strcmp(word, pc_occlusion_names[k]))
            return k;
    if (n == 1 && word[0] >= '0' && word[0] <= '3')
        return word[0] - '0';
    if (!strcmp(word, "true"))
        return PC_OCCLUSION_FADE;
    if (!strcmp(word, "false"))
        return PC_OCCLUSION_OFF;
    return -1;
}

/* Numbers in `text` (blanks, commas or a JSON array between them). */
static int numbers(const char* text, float* out, int max)
{
    int n = 0;
    while (*text && n < max) {
        while (*text && !(*text == '-' || *text == '.' || (*text >= '0' && *text <= '9'))) {
            if (*text == '}' || (n && *text == ']'))
                return n;
            text++;
        }
        if (!*text)
            break;
        char* end;
        float v = strtof(text, &end);
        if (end == text)
            break;
        out[n++] = v;
        text = end;
    }
    return n;
}

int pc_occlusion_setting(const char* key, const char* value)
{
    if (!strcmp(key, "occlusion")) {
        int m = mode_from(value);
        if (m >= 0)
            pc_occlusion = m;
        return 1;
    }
    for (int i = 0; i < TUNABLES; i++) {
        const tunable* t = &tunables[i];
        if (strcmp(key, t->key))
            continue;
        float v[3];
        int n = numbers(value, v, t->f ? t->count : 1);
        if (n < (t->f ? t->count : 1))
            return 1;
        for (int k = 0; k < n; k++)
            v[k] = v[k] < t->lo ? t->lo : v[k] > t->hi ? t->hi : v[k];
        if (t->i)
            *t->i = (int)lroundf(v[0]);
        else
            memcpy(t->f, v, (size_t)t->count * sizeof *v);
        return 1;
    }
    return 0;
}

void pc_occlusion_settings_write(FILE* f)
{
    fprintf(f, "occlusion %s\n", pc_occlusion_names[pc_occlusion & 3]);
    for (int i = 0; i < TUNABLES; i++) {
        const tunable* t = &tunables[i];
        if (t->i)
            fprintf(f, "%s %d\n", t->key, *t->i);
        else if (t->count == 1)
            fprintf(f, "%s %g\n", t->key, t->f[0]);
        else
            fprintf(f, "%s %g %g %g\n", t->key, t->f[0], t->f[1], t->f[2]);
    }
}

const char* pc_occlusion_key(int index)
{
    return index == 0 ? "occlusion" : index - 1 < TUNABLES ? tunables[index - 1].key : NULL;
}

int pc_occlusion_json(char* out, int size)
{
    int used = snprintf(out, (size_t)size, "{\"occlusion\":\"%s\"", pc_occlusion_names[pc_occlusion & 3]);
    for (int i = 0; i < TUNABLES && used < size - 96; i++) {
        const tunable* t = &tunables[i];
        if (t->i)
            used += snprintf(out + used, (size_t)(size - used), ",\"%s\":%d", t->key, *t->i);
        else if (t->count == 1)
            used += snprintf(out + used, (size_t)(size - used), ",\"%s\":%g", t->key, t->f[0]);
        else
            used += snprintf(out + used, (size_t)(size - used), ",\"%s\":[%g,%g,%g]", t->key, t->f[0], t->f[1], t->f[2]);
    }
    const pc_occlusion_subject* s;
    int n = pc_occlusion_subjects_get(&s);
    if (used < size - 32)
        used += snprintf(out + used, (size_t)(size - used), ",\"subjects\":[");
    for (int i = 0; i < n && used < size - 256; i++)
        used += snprintf(out + used, (size_t)(size - used),
            "%s{\"slot\":%d,\"enemy\":%d,\"center\":[%.1f,%.1f,%.1f],\"half_w\":%.1f,\"half_h\":%.1f,"
            "\"fade\":%.4f,\"fade_previous\":%.4f,\"hidden\":%.4f,\"rays_blocked\":%d}",
            i ? "," : "", s[i].slot, s[i].enemy, s[i].center[0], s[i].center[1], s[i].center[2], s[i].half_w,
            s[i].half_h, s[i].fade, s[i].fade_previous, s[i].hidden, s[i].rays_blocked);
    if (used < size - 4)
        used += snprintf(out + used, (size_t)(size - used), "]}");
    return used;
}
