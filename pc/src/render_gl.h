/* Between render.c and the hardware rasterizer (gpu_hw.c): how a native list
 * is drawn into the scaled VRAM target. Not for game-side code (render.h). */
#ifndef ROOD_RENDER_GL_H
#define ROOD_RENDER_GL_H
#include "gl_api.h"
#include "render.h"

typedef struct {
    int scale; /* the target's internal resolution multiple */
    int clip[4]; /* drawing area (native VRAM pixels) */
    int ox, oy; /* drawing offset */
    unsigned window; /* texture window */
    int mask_set;
    GLuint vram; /* native VRAM (R16UI) */
    int perspective, filtering;
    /* Room lists: the colour/G-buffer target with its own depth (reset over
     * the drawing area first). */
    GLuint fbo;
    /* Object lists: pass 1 draws colour and G-buffer into `object_fbo`, whose
     * depth attachment is `surface_depth` (the native surfaces so far: room,
     * then objects), depth-tested; pass 2 writes the visible fragments' depth
     * into `fbo`'s depth (what post effects read). */
    GLuint object_fbo, surface_depth;
    /* A copy of surface_depth from before the list (instances drawn in PS1
     * order test against it, not against themselves). */
    GLuint surface_before;
    const render_camera* camera; /* NULL: the list's own (frame interpolation passes one) */
    float t; /* palette blend from `previous` (0) to the current palette (1) */
    int count_stats; /* the live frame (not a replay) */
} render_target;
/* Returns the instances drawn, or -1 when the list is gone or unavailable. */
int render_draw_list(uint32_t id, const render_target* target);
/* Entries [first, first + count) of the list's translucent sequence into
 * `fbo` (depth ALWAYS; hidden behind `surface_depth`, the native surfaces
 * so far, read as a texture): opaque texels write colour, G-buffer and
 * depth, translucent texels blend colour only. Returns the polygons
 * submitted, -1 when the list is gone. */
int render_draw_translucent(uint32_t id, int first, int count, const render_target* target);
/* The frame is complete (statistics roll over, the frame serial advances);
 * `hardware` carries the hardware path's packet counters (legacy_* and
 * skipped_* fields). */
void render_frame_end(const render_stats* hardware);
int render_is_marker(const uint32_t* packet);
/* A translucent marker (render_list_link_translucent): returns 1 and its
 * list and range. */
int render_translucent_marker(const uint32_t* packet, uint32_t* list, int* first, int* count);
#endif
