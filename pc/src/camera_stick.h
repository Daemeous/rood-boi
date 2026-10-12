/* Right-stick camera (camera_stick.c): continuous yaw and zoom on top of the
 * game's own follow camera. See the file comment there. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* The game's spherical camera (BATTLE 146C.h _sphericalCamera, all ints, the
 * same layout on every architecture). */
typedef struct {
    int yaw, pitch, distance, mode;
} pc_spherical_values;
typedef struct {
    pc_spherical_values values;
    /* While L1/R1 turn: delta.yaw 1 (R1, decreasing) or 2 (L1, increasing),
     * delta.pitch the target yaw, delta.distance the step per frame;
     * delta.mode is the Select distance toggle's state. */
    pc_spherical_values delta;
    int initial_distance, unk24, speed;
} pc_spherical_camera;

/* Settings (project_settings.cpp keys camera_stick_*). */
extern int pc_camera_stick; /* master switch: the right stick turns and zooms */
extern float pc_camera_stick_speed; /* degrees per second at full deflection */
extern float pc_camera_stick_deadzone; /* 0..0.9 of the stick's travel */
extern float pc_camera_stick_curve; /* response exponent (1 linear) */
extern int pc_camera_stick_invert_x, pc_camera_stick_invert_y;
extern int pc_camera_stick_zoom; /* stick Y zooms */
extern float pc_camera_stick_zoom_speed; /* zoom factor doublings per second */
extern float pc_camera_stick_zoom_min, pc_camera_stick_zoom_max; /* x the game's distance */
extern float pc_camera_stick_zoom_pitch; /* degrees steeper fully out, flatter fully in */
extern int pc_camera_stick_collision; /* pull in before room geometry when zoomed out */
extern int pc_camera_stick_r3; /* R3 resets the camera (else the game's R3: first-person view) */
extern float pc_camera_stick_idle_reset; /* seconds without stick input, then reset (0 off) */
extern int pc_camera_stick_keys; /* keyboard J/L turn, I/K zoom, O resets */

/* BATTLE func_8006E158 (the L1/R1 handler), once per game frame wherever the
 * game turns its follow camera; locked: CAMERA_INPUT_LOCKED(). */
void pc_camera_stick_tick(void* spherical, int locked);
/* An L1/R1 turn just started (func_8006E158): from a yaw the stick left
 * between the 45-degree lines, R1 aims at the nearest line below. */
void pc_camera_stick_turn(void* spherical);
/* Before the follow camera is placed from its spherical values: the zoom,
 * the zoom's pitch and the collision pull-in applied to a copy. look_at: the
 * camera's look-at point (VECTOR, 20.12 fixed). */
void pc_camera_stick_place(void* values, const int* look_at);
/* The PS1 pad bits of this vsync (boot_window.c): R3 goes to the camera
 * when it resets with R3. */
unsigned pc_camera_stick_buttons(unsigned buttons);
/* Reset now (R3, the editor's button, RPC). */
void pc_camera_stick_reset(void);
/* RPC camera_stick: the state as a JSON object. */
int pc_camera_stick_json(char* out, int size);

/* Pure helpers (camera_stick_probe). */
float pc_camera_stick_shape(float value, float deadzone, float curve);
/* Nearest hit of the ray origin + t * direction (unit) with triangle
 * (a, b, c) for t in (t0, t1], else -1. Two-sided. */
float pc_camera_stick_ray_triangle(const float origin[3], const float direction[3], const float a[3],
    const float b[3], const float c[3], float t0, float t1);
/* The yaw the reset turns to (the game's 45-degree grid, nearest) and the
 * L1/R1 animation that gets there: *direction 0 (already there), 1
 * (decreasing, as R1) or 2 (increasing, as L1). */
int pc_camera_stick_reset_target(int yaw, int* direction);

#ifdef __cplusplus
}
#endif
