/* What the renderer has to say about the picture it shows (T1 R4e), asked for at the frame header's fill (PC_RQ_FRAME_STATE, renderer_rq.h) so
 * the game thread's post-processing and glow gathering never read the renderer's state: whether the shown frame drew 3D, whether it is the
 * menu's model view, the game's screen fade, and the emissive triangles of the frame (the effects: flames) with their surfaces and sources. */
#ifndef PC_RENDERER_FRAME_STATE_H
#define PC_RENDERER_FRAME_STATE_H
#include <stdint.h>
#define PC_FRAME_EMISSIVES 4096
typedef struct {
    uint8_t rgb[9], uv[6], code;
    uint16_t tpage, clut;
} pc_frame_surface; /* (gpu_hw.c pc_hw_surface) */
typedef struct {
    int world, menu_model;
    float fade, fade_rect[4];
    int emissive_count;
    float emissives[PC_FRAME_EMISSIVES * 9];
    pc_frame_surface surfaces[PC_FRAME_EMISSIVES];
    uint32_t sources[PC_FRAME_EMISSIVES];
} pc_renderer_frame_state;
void pc_hw_frame_state(pc_renderer_frame_state* out); /* gpu_hw.c: the renderer's side of the request */
#endif
