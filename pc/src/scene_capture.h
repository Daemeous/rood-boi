/* Captured room geometry (scene_capture.c), shared with the editor view. */
#ifndef ROOD_SCENE_CAPTURE_H
#define ROOD_SCENE_CAPTURE_H
#include <stdint.h>

typedef struct {
    float position[3]; /* world space */
    uint8_t rgb[3]; /* baked vertex colour (128 = neutral texture modulation) */
    uint8_t u, v;
} scene_vertex;
typedef struct {
    scene_vertex vertex[3];
    uint16_t tpage, clut;
    uint16_t group;
    uint32_t flags; /* group flags: 0x8000 alternate set, low byte camera-facing cutaway mask */
    uint8_t code; /* GPU polygon command (bit 1 semi-transparent, bit 0 raw texture) */
    uint8_t quad; /* 0 a triangle; a quad's halves: 1 (vertex 1-2 is its diagonal), 2 (vertex 0-2) */
    uint32_t polygon; /* the polygon's byte offset in the room data block */
} scene_triangle;

#ifdef __cplusplus
extern "C" {
#endif
const scene_triangle* pc_scene_triangle_data(int* count);
int pc_scene_serial(void);
/* Room texture effects (room_effects.c) animate some triangles' UVs: the
 * triangles' UVs follow the drawn frame (updated once per game frame), the
 * indices of those triangles, and a serial that changes whenever one did
 * (the ray-traced scene re-reads their UVs). */
const int* pc_scene_animated(int* count);
int pc_scene_uv_serial(void);
/* The room triangle under world point `p` (within `slack` units of its
 * plane, inside its edges), -1 when none. */
int pc_scene_triangle_at(const float p[3], float slack);
unsigned pc_scene_facing(void);
int pc_scene_camera_inverse(float inverse[3][3]);
int pc_scene_camera_get(float rotation[3][3], float translation[3], float* h, float* ofx, float* ofy);
/* The GTE camera of the frame the game is building, without the presented
 * picture's override (pc_scene_camera_get: the picture's camera). */
int pc_scene_game_camera(float rotation[3][3], float translation[3], float* h, float* ofx, float* ofy);
int pc_scene_game_inverse(float inverse[3][3]);
int pc_scene_game_projection(float* h, float* ofx, float* ofy);
#ifdef __cplusplus
}
#endif
#endif
