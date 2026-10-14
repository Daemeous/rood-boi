/* Right-stick camera: the right stick's X turns the game's follow camera
 * around Ashley at a speed proportional to the deflection, its Y zooms in
 * and out; R3 turns back to the game's 45-degree grid and zoom.
 *
 * How the game's camera works (BATTLE 146C.c): the follow camera is a
 * spherical camera around a look-at point above Ashley,
 * vs_battle_cameraCurrentSpherical (yaw 0..4095 = 360 degrees, pitch -384,
 * distance 0x600 or 0x900 by the Select toggle). func_8006E158 is its input:
 * L1/R1 start a turn to the next multiple of 0x200 (45 degrees) that the
 * following frames animate (delta.yaw the direction, delta.pitch the target,
 * delta.distance the step, up to 40 units a frame); Select eases the
 * distance between the two. _setCameraPositionFromAngles places the camera
 * from those values, func_80069D78 derives scratch camera.angles.vy (yaw,
 * 0x1F800058) back from the placed camera, and scene_submit.inc picks the
 * cutaway walls (facing mask) from that yaw. Ashley's movement is relative
 * to the same spherical yaw.
 *
 * So the stick's yaw goes into the spherical yaw itself, in
 * func_8006E158 (pc_camera_stick_tick), only where the game would start an
 * L1/R1 turn (not CAMERA_INPUT_LOCKED: events, combat camera modes 4 and
 * 11) and never during one: the facing mask, movement directions, the
 * targeting sphere and room entries (which take delta.pitch as the yaw) all
 * see the same continuous yaw, and L1/R1 still turn to the next 45 degrees.
 * The zoom is not written into the game's state: it scales the distance of
 * a copy each time the camera is placed from the current spherical values
 * (pc_camera_stick_place), with a slight pitch change and a pull-in before
 * room geometry beyond the game's own distance, so the Select toggle, room
 * entries and scripted cameras keep their own distances, and while the game
 * holds the camera (no unlocked turn input for a few frames) the zoom eases
 * back to 1. With the stick untouched and the zoom at 1 nothing changes:
 * the game runs exactly as the original.
 *
 * The first-person view (BATTLE func_8006EC7C, field mode 7) has no follow
 * camera (func_8006E158 does not run there) and ignores the right stick of
 * a digital pad: pc_camera_stick_first_person turns the stick into yaw and
 * pitch steps on top of the D-pad's, always (not behind the camera_stick
 * switch, which is about the follow camera), with this camera's dead zone,
 * curve and speed. While that view runs, R3 is the game's again (it leaves
 * the view). */
#include "camera_stick.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "scene_capture.h"

int pc_camera_stick = 1;
float pc_camera_stick_speed = 120.0f;
float pc_camera_stick_deadzone = 0.15f;
float pc_camera_stick_curve = 1.6f;
int pc_camera_stick_invert_x, pc_camera_stick_invert_y;
int pc_camera_stick_zoom = 1;
float pc_camera_stick_zoom_speed = 1.0f;
float pc_camera_stick_zoom_min = 0.6f, pc_camera_stick_zoom_max = 1.5f;
float pc_camera_stick_zoom_pitch = 5.0f;
int pc_camera_stick_collision = 1;
int pc_camera_stick_r3 = 1;
float pc_camera_stick_idle_reset;
int pc_camera_stick_keys = 1;

/* Inputs and the scene, weak so the CTest probe links this file alone. */
extern unsigned pc_vsync_count(void) __attribute__((weak));
extern int pc_window_stick(float out[2]) __attribute__((weak)); /* gamepad right stick and keys */
extern int pc_rpc_stick(float out[2]) __attribute__((weak)); /* RPC input {right_stick} */
extern const scene_triangle* pc_scene_triangle_data(int* count) __attribute__((weak));
extern unsigned pc_scene_facing(void) __attribute__((weak));
extern int pc_scene_serial(void) __attribute__((weak));

#define GAME_FAR 0x900 /* the game's own farthest follow distance (Select) */
#define ACTIVE_VSYNCS 8 /* turn input this recent: the stick has the camera */
#define R3_BIT (1u << 10)
#define PI 3.14159265358979f

static struct {
    unsigned last_tick, last_place, last_input, r3_vsync;
    int ticked, placed, r3_down, r3_latched, reset, offset, locked;
    float rate, fraction; /* yaw: smoothed units per second, sub-unit carry */
    float zoom, zoom_applied, arm; /* the stick's zoom; the zoom placed; spring arm distance */
    float stick[2], shaped[2];
    int room_serial;
    float room_extent, room_zoom_max;
    /* The last placement (RPC). */
    int game_distance, placed_distance, placed_pitch, placed_yaw;
    float desired, hit;
    int ticks, turns, resets;
    int stick_yaw, stick_yaw_valid; /* the yaw the stick last wrote */
    /* First-person look (pc_camera_stick_first_person). */
    unsigned fp_vsync;
    int fp_ticked, fp_looks, fp_yaw, fp_pitch;
    float fp_fraction[2];
} s = { .zoom = 1, .zoom_applied = 1, .room_zoom_max = 1.5f, .hit = -1 };

static unsigned now(void) { return pc_vsync_count ? pc_vsync_count() : 0; }

float pc_camera_stick_shape(float value, float deadzone, float curve)
{
    float a = fabsf(value);
    if (deadzone < 0)
        deadzone = 0;
    if (deadzone > 0.9f)
        deadzone = 0.9f;
    if (a <= deadzone)
        return 0;
    a = (a - deadzone) / (1 - deadzone);
    if (a > 1)
        a = 1;
    a = powf(a, curve > 0.1f ? curve : 0.1f);
    return value < 0 ? -a : a;
}

float pc_camera_stick_ray_triangle(const float o[3], const float d[3], const float a[3], const float b[3],
    const float c[3], float t0, float t1)
{
    float e1[3], e2[3], p[3], q[3], tv[3];
    for (int k = 0; k < 3; k++)
        e1[k] = b[k] - a[k], e2[k] = c[k] - a[k], tv[k] = o[k] - a[k];
    p[0] = d[1] * e2[2] - d[2] * e2[1];
    p[1] = d[2] * e2[0] - d[0] * e2[2];
    p[2] = d[0] * e2[1] - d[1] * e2[0];
    float det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (fabsf(det) < 1e-6f)
        return -1;
    float inv = 1 / det;
    float u = (tv[0] * p[0] + tv[1] * p[1] + tv[2] * p[2]) * inv;
    if (u < 0 || u > 1)
        return -1;
    q[0] = tv[1] * e1[2] - tv[2] * e1[1];
    q[1] = tv[2] * e1[0] - tv[0] * e1[2];
    q[2] = tv[0] * e1[1] - tv[1] * e1[0];
    float v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (v < 0 || u + v > 1)
        return -1;
    float t = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return t > t0 && t <= t1 ? t : -1;
}

int pc_camera_stick_reset_target(int yaw, int* direction)
{
    yaw &= 0xFFF;
    int below = yaw & ~0x1FF;
    if (yaw == below) {
        *direction = 0;
        return yaw;
    }
    if (yaw - below < 0x100) {
        *direction = 1; /* as R1: decreasing */
        return below;
    }
    *direction = 2; /* as L1: increasing (0x1000 wraps to 0 when the turn ends) */
    return below + 0x200;
}

void pc_camera_stick_reset(void) { s.reset = 1; }

/* The first-person view ran within the last game frame or two (it calls
 * pc_camera_stick_first_person every game frame). */
static int first_person(void) { return s.fp_ticked && now() - s.fp_vsync <= 3; }

unsigned pc_camera_stick_buttons(unsigned buttons)
{
    if (!pc_camera_stick || !pc_camera_stick_r3)
        return buttons;
    int down = (buttons & R3_BIT) != 0;
    if (first_person()) {
        /* In the first-person view R3 is the game's (it leaves the view, as
         * Start and Cross do): no follow camera to reset there. */
        s.r3_down = down;
        return buttons;
    }
    if (down && !s.r3_down) {
        s.r3_latched = 1;
        s.r3_vsync = now();
    }
    s.r3_down = down;
    return buttons & ~R3_BIT;
}

/* The room's horizontal size bounds how far out the zoom goes (the scene's
 * triangles, captured once per room). */
static void room_limits(void)
{
    int serial = pc_scene_serial ? pc_scene_serial() : 0;
    if (serial == s.room_serial && s.room_extent > 0)
        return;
    s.room_serial = serial;
    int count = 0;
    const scene_triangle* t = pc_scene_triangle_data ? pc_scene_triangle_data(&count) : NULL;
    float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f };
    for (int i = 0; t && i < count; i++)
        for (int v = 0; v < 3; v++)
            for (int k = 0; k < 2; k++) {
                float x = t[i].vertex[v].position[k * 2];
                lo[k] = x < lo[k] ? x : lo[k];
                hi[k] = x > hi[k] ? x : hi[k];
            }
    s.room_extent = count ? fmaxf(hi[0] - lo[0], hi[1] - lo[1]) : 0;
}

static float zoom_max(void)
{
    float configured = pc_camera_stick_zoom_max < 1 ? 1 : pc_camera_stick_zoom_max;
    /* Out to the game's far distance plus the room's size; rooms the
     * capture does not know: the setting. */
    float room = s.room_extent > 0 ? 1 + s.room_extent / GAME_FAR : configured;
    s.room_zoom_max = room < configured ? room : configured;
    return s.room_zoom_max;
}

static float zoom_min(void)
{
    float z = pc_camera_stick_zoom_min;
    return z < 0.3f ? 0.3f : z > 1 ? 1 : z;
}

static void read_stick(float out[2])
{
    out[0] = out[1] = 0;
    float v[2];
    if (pc_window_stick && pc_window_stick(v))
        out[0] += v[0], out[1] += v[1];
    if (pc_rpc_stick && pc_rpc_stick(v))
        out[0] += v[0], out[1] += v[1];
    for (int k = 0; k < 2; k++)
        out[k] = out[k] > 1 ? 1 : out[k] < -1 ? -1 : out[k];
}

void pc_camera_stick_tick(void* spherical, int locked)
{
    pc_spherical_camera* camera = spherical;
    unsigned t = now();
    unsigned gap = s.ticked ? t - s.last_tick : 2;
    s.ticked = 1;
    s.last_tick = t;
    s.locked = locked;
    s.ticks++;
    float dt = (gap < 1 ? 1 : gap > 8 ? 2 : gap) / 59.94f;
    read_stick(s.stick);
    if (!pc_camera_stick || locked) {
        s.rate = s.fraction = 0;
        s.shaped[0] = s.shaped[1] = 0;
        return;
    }
    if (!s.last_input)
        s.last_input = t;
    float x = pc_camera_stick_shape(s.stick[0], pc_camera_stick_deadzone, pc_camera_stick_curve);
    float y = pc_camera_stick_shape(s.stick[1], pc_camera_stick_deadzone, pc_camera_stick_curve);
    if (pc_camera_stick_invert_x)
        x = -x;
    if (pc_camera_stick_invert_y)
        y = -y;
    s.shaped[0] = x, s.shaped[1] = y;
    if (x || y)
        s.last_input = t;

    /* R3, or the idle timer: back to the 45-degree grid and zoom 1. */
    if (s.r3_latched) {
        s.r3_latched = 0;
        if (t - s.r3_vsync <= 10)
            s.reset = 1;
    }
    if (pc_camera_stick_idle_reset > 0 && s.offset && !x && !y
        && (t - s.last_input) / 59.94f >= pc_camera_stick_idle_reset)
        s.reset = 1;
    if (s.reset) {
        s.reset = 0;
        s.offset = 0;
        s.resets++;
        s.zoom = 1;
        s.rate = s.fraction = 0;
        int direction;
        int target = pc_camera_stick_reset_target(camera->values.yaw, &direction);
        if (direction && camera->delta.yaw == 0) {
            /* The game's own L1/R1 animation turns there (from a multiple of
             * 8: pc_camera_stick_turn). */
            camera->values.yaw &= 0xFF8;
            camera->delta.yaw = direction;
            camera->delta.pitch = target;
        }
        return;
    }

    /* Yaw: continuous, between L1/R1 turns. Right turns as R1 does. */
    if (camera->delta.yaw == 0) {
        float target = -x * pc_camera_stick_speed * (4096.0f / 360.0f);
        float k = dt / 0.06f;
        s.rate += (target - s.rate) * (k > 1 ? 1 : k);
        if (!x && fabsf(s.rate) < 4)
            s.rate = 0;
        s.fraction += s.rate * dt;
        int step = (int)s.fraction;
        if (step) {
            s.fraction -= (float)step;
            camera->values.yaw = (camera->values.yaw + step) & 0xFFF;
            s.stick_yaw = camera->values.yaw;
            s.stick_yaw_valid = 1;
            /* A room entry takes the last turn's target as its yaw. */
            camera->delta.pitch = camera->values.yaw;
            s.offset = 1;
            s.turns++;
        }
    } else {
        s.rate = s.fraction = 0;
    }

    /* Zoom: stick down zooms out, within this room's limits. */
    room_limits();
    float z = s.zoom;
    if (pc_camera_stick_zoom && y) {
        z *= exp2f(y * pc_camera_stick_zoom_speed * dt);
        s.offset = 1;
    }
    float lo = zoom_min(), hi = zoom_max();
    s.zoom = z < lo ? lo : z > hi ? hi : z;
}

void pc_camera_stick_turn(void* spherical)
{
    pc_spherical_camera* camera = spherical;
    /* The game aims R1 at (yaw - 0x200) & ~0x1FF: from between two lines
     * that is the second line down. Only for a yaw the stick set. */
    if (!(camera->values.yaw & 0x1FF) || !s.stick_yaw_valid || camera->values.yaw != s.stick_yaw)
        return;
    if (camera->delta.yaw == 1)
        camera->delta.pitch = camera->values.yaw & ~0x1FF;
    /* The turn steps in multiples of 8 and ends with yaw &= 0xE00, which
     * rounds an overshoot below the line down to the line before it: start
     * on a multiple of 8, as the game's own yaws are. */
    camera->values.yaw &= ~7;
}

void pc_camera_stick_first_person(int* yaw, int* pitch)
{
    unsigned t = now();
    unsigned gap = t - s.fp_vsync;
    if (!s.fp_ticked || gap > 8) { /* entering the view: no carry from before */
        s.fp_fraction[0] = s.fp_fraction[1] = 0;
        gap = 2;
    }
    s.fp_ticked = 1;
    s.fp_vsync = t;
    float dt = (gap < 1 ? 1 : gap) / 59.94f;
    float v[2];
    read_stick(v);
    /* The follow camera's dead zone, curve and speed; not its inversions
     * (turning and zooming): right looks right, down looks down. */
    float rate = pc_camera_stick_speed * (4096.0f / 360.0f) * dt;
    s.fp_fraction[0] += pc_camera_stick_shape(v[0], pc_camera_stick_deadzone, pc_camera_stick_curve) * rate;
    s.fp_fraction[1] += pc_camera_stick_shape(v[1], pc_camera_stick_deadzone, pc_camera_stick_curve) * rate;
    *yaw = (int)s.fp_fraction[0];
    *pitch = (int)s.fp_fraction[1];
    s.fp_fraction[0] -= (float)*yaw;
    s.fp_fraction[1] -= (float)*pitch;
    s.fp_yaw = *yaw, s.fp_pitch = *pitch;
    if (*yaw || *pitch)
        s.fp_looks++;
}

/* Room triangles the camera may not pass beyond: drawn under this frame's
 * facing mask (the cutaway walls are not drawn, so passing them shows
 * nothing wrong), not disabled (0x1100) and not the alternate set. */
static int solid(const scene_triangle* t, unsigned facing)
{
    unsigned flags = t->flags;
    if (flags & 0x9100)
        return 0;
    return (facing >> 16) || !((flags ^ 255) & facing & 255);
}

/* The nearest room surface along the arm beyond `from`, -1 when none. */
static float arm_hit(const float origin[3], const float direction[3], float from, float to)
{
    int count = 0;
    const scene_triangle* t = pc_scene_triangle_data ? pc_scene_triangle_data(&count) : NULL;
    unsigned facing = pc_scene_facing ? pc_scene_facing() : 0x10000;
    float best = -1;
    for (int i = 0; t && i < count; i++) {
        if (!solid(&t[i], facing))
            continue;
        float hit = pc_camera_stick_ray_triangle(origin, direction, t[i].vertex[0].position,
            t[i].vertex[1].position, t[i].vertex[2].position, from, best > 0 ? best : to);
        if (hit > 0)
            best = hit;
    }
    return best;
}

void pc_camera_stick_place(void* spherical_values, const int* look_at)
{
    pc_spherical_values* values = spherical_values;
    unsigned t = now();
    unsigned gap = s.placed ? t - s.last_place : 0;
    s.placed = 1;
    s.last_place = t;
    float dt = (gap > 4 ? 4 : gap) / 59.94f;
    int active = pc_camera_stick && s.ticked && !s.locked && t - s.last_tick <= ACTIVE_VSYNCS;
    float target = active ? s.zoom : 1;
    float k = dt / 0.1f;
    s.zoom_applied += (target - s.zoom_applied) * (k > 1 ? 1 : k);
    if (fabsf(s.zoom_applied - target) < 0.002f)
        s.zoom_applied = target;
    s.game_distance = values->distance;
    s.desired = (float)values->distance;
    s.hit = -1;
    if (s.zoom_applied == 1) {
        /* The game's own camera, untouched. */
        s.arm = 0;
        s.placed_distance = values->distance;
        s.placed_pitch = values->pitch;
        s.placed_yaw = values->yaw;
        return;
    }
    float zoom = s.zoom_applied;
    float desired = values->distance * zoom;
    /* Slightly steeper zoomed out, flatter zoomed in (pitch < 0: above). */
    float lo = zoom_min(), hi = s.room_zoom_max > 1 ? s.room_zoom_max : 1.5f;
    float u = zoom >= 1 ? (zoom - 1) / (hi > 1.001f ? hi - 1 : 1) : -(1 - zoom) / (1 - lo > 0.001f ? 1 - lo : 1);
    u = u > 1 ? 1 : u < -1 ? -1 : u;
    int pitch = values->pitch - (int)lrintf(u * pc_camera_stick_zoom_pitch * (4096.0f / 360.0f));
    if (pitch < -896)
        pitch = -896;
    if (pitch > 0)
        pitch = 0;
    values->pitch = pitch;

    /* Spring arm: beyond the game's own distance, stop short of the first
     * solid room surface; pull in at once, let out gradually. */
    float placed = desired;
    float base = (float)values->distance;
    if (pc_camera_stick_collision && desired > base) {
        float yaw = values->yaw * (2 * PI / 4096), p = pitch * (2 * PI / 4096);
        float d[3] = { sinf(yaw), tanf(p), cosf(yaw) };
        float n = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        for (int i = 0; i < 3; i++)
            d[i] /= n;
        float o[3] = { look_at[0] / 4096.0f, look_at[1] / 4096.0f, look_at[2] / 4096.0f };
        const float margin = 64;
        s.hit = arm_hit(o, d, base, desired + margin);
        float limit = s.hit > 0 ? fmaxf(base, s.hit - margin) : desired;
        if (s.arm <= 0 || limit < s.arm)
            s.arm = limit;
        else
            s.arm = fminf(limit, s.arm + 1500 * dt);
        placed = fminf(desired, s.arm);
    } else {
        s.arm = desired;
    }
    s.desired = desired;
    values->distance = (int)lrintf(placed);
    s.placed_distance = values->distance;
    s.placed_pitch = values->pitch;
    s.placed_yaw = values->yaw;
}

int pc_camera_stick_json(char* out, int size)
{
    unsigned t = now();
    int active = pc_camera_stick && s.ticked && !s.locked && t - s.last_tick <= ACTIVE_VSYNCS;
    return snprintf(out, (size_t)size,
        "{\"enabled\":%d,\"speed\":%g,\"deadzone\":%g,\"curve\":%g,\"invert_x\":%d,\"invert_y\":%d,"
        "\"zoom\":%d,\"zoom_speed\":%g,\"zoom_min\":%g,\"zoom_max\":%g,\"zoom_pitch\":%g,\"collision\":%d,"
        "\"r3_reset\":%d,\"idle_reset\":%g,\"keys\":%d,"
        "\"active\":%d,\"locked\":%d,\"stick\":[%g,%g],\"shaped\":[%g,%g],\"rate\":%g,"
        "\"zoom_target\":%g,\"zoom_applied\":%g,\"room_zoom_max\":%g,\"room_extent\":%g,"
        "\"game_distance\":%d,\"desired\":%g,\"hit\":%g,\"arm\":%g,\"distance\":%d,\"pitch\":%d,\"yaw\":%d,"
        "\"offset\":%d,\"ticks\":%d,\"turns\":%d,\"resets\":%d,\"vsyncs_since_tick\":%u,\"stick_yaw\":%d,"
        "\"first_person\":%d,\"fp_looks\":%d,\"fp_step\":[%d,%d]}",
        pc_camera_stick, pc_camera_stick_speed, pc_camera_stick_deadzone, pc_camera_stick_curve,
        pc_camera_stick_invert_x, pc_camera_stick_invert_y, pc_camera_stick_zoom, pc_camera_stick_zoom_speed,
        pc_camera_stick_zoom_min, pc_camera_stick_zoom_max, pc_camera_stick_zoom_pitch, pc_camera_stick_collision,
        pc_camera_stick_r3, pc_camera_stick_idle_reset, pc_camera_stick_keys, active, s.locked, s.stick[0],
        s.stick[1], s.shaped[0], s.shaped[1], s.rate, s.zoom, s.zoom_applied, s.room_zoom_max, s.room_extent,
        s.game_distance, s.desired, s.hit, s.arm, s.placed_distance, s.placed_pitch, s.placed_yaw, s.offset,
        s.ticks, s.turns, s.resets, s.ticked ? t - s.last_tick : 0, s.stick_yaw_valid ? s.stick_yaw : -1,
        first_person(), s.fp_looks, s.fp_yaw, s.fp_pitch);
}
