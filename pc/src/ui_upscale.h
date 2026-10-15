/* 2D / UI texture magnification (ui_upscale.c): MMPX (McGuire & Gagiu 2021)
 * for the textures of 2D primitives (HUD, menus, fonts, talk bubbles,
 * icons), from the game's own VRAM at run time. gpu_hw.c asks for each 2D
 * textured triangle's texel region; the region, decoded through its CLUT, is
 * magnified once into an atlas (GPU compute, CPU fallback) and cached by
 * content, so frames whose 2D does not change only hash it. */
#ifndef ROOD_UI_UPSCALE_H
#define ROOD_UI_UPSCALE_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Options (overlay Graphics > 2D / UI, RPC graphics, settings ui_upscale /
 * ui_subpixel). */
enum { PC_UI_NEAREST = 0, PC_UI_MMPX = 1 };
extern int pc_ui_upscale;  /* PC_UI_*; nearest keeps the original look */
extern int pc_ui_subpixel; /* text glyphs keep their unrounded 16.16 placement (else whole pixels) */
extern const char* const pc_ui_upscale_names[2];

/* The atlas magnification for internal resolution `scale` (2, 4 or 8; at
 * least 4 while text or icons come from fonts), or 0 when 2D is not
 * magnified (nearest, scale 1). */
int pc_ui_upscale_factor(int scale);
/* A 2D textured primitive samples texels [u0, u1] x [v0, v1] (inclusive) of
 * page `tpage` / CLUT `clut`: 1 with offset[] (atlas origin - u0/v0, in
 * texels; the shader multiplies by the factor) when its magnified copy is
 * (or will be, by pc_ui_upscale_prepare) in the atlas, 2 when that is text
 * or an icon drawn from a font (distance texels, ui_text.h); 0 to sample it
 * nearest; -1 when the atlas is full: the caller draws what it queued, calls
 * pc_ui_upscale_reset and asks again. */
int pc_ui_upscale_lookup(const uint16_t* vram, unsigned tpage, unsigned clut, unsigned window, int u0, int v0,
    int u1, int v1, int scale, int16_t offset[2]);
/* A laid-out glyph's image (ui_text.h pc_ui_text_quad.image): as
 * pc_ui_upscale_lookup (2: found), offset[] its atlas origin (its texels
 * start at u, v = 0, 0). */
int pc_ui_upscale_lookup_image(int image, int scale, int16_t offset[2]);
/* Record the work for regions looked up since the last call (before a draw
 * samples them). */
void pc_ui_upscale_prepare(void);
void pc_ui_upscale_reset(void);
/* VRAM tiles (gpu_hw.c's 64 x 256 tile bits) changed. */
void pc_ui_upscale_tiles_changed(uint32_t tiles);
/* A drawing area used this frame (render-to-texture sources are not UI). */
void pc_ui_upscale_draw_area(const int* clip);
/* A new ordering table (game frame). */
void pc_ui_upscale_frame(void);
/* What the draw shaders multiply FLAG_UI offsets by: the atlas's factor
 * while MMPX is on, else 0 (nearest, whatever the primitives recorded). */
int pc_ui_upscale_draw_factor(void);
/* The atlas for the draw shaders: GL texture name (0: none). */
unsigned pc_ui_upscale_gl_texture(void);
#ifdef ROOD_VULKAN
/* Vulkan image view of the atlas (VK_NULL_HANDLE: none). */
void* pc_ui_upscale_vk_view(void);
#endif

typedef struct {
    long lookups, hits, misses, bypassed, over_budget, resets;
    long entries, texels; /* in the atlas (source texels) */
    long frame_lookups, frame_misses; /* the last frame */
    double frame_ms, miss_ms; /* CPU time in lookups / magnifying, the last frame */
    int factor, gpu; /* gpu: 1 Vulkan compute, 2 GL compute, 0 CPU */
    long precise, fractional; /* sub-pixel text: glyph quads placed, of them between pixels */
} pc_ui_upscale_stats;
void pc_ui_upscale_get_stats(pc_ui_upscale_stats* s);
/* Self-check (RPC ui_upscale verify): every atlas entry read back and
 * compared with the CPU reference of MMPX; returns entries checked and sets
 * the mismatching texels and texels whose value is not in the entry's
 * source (palette violations). */
int pc_ui_upscale_verify(long* mismatches, long* foreign);
/* Sub-pixel 2D: a quad packet's unrounded corners (x, y pairs in packet
 * order, VRAM pixels before the drawing offset), and their lookup for a
 * packet whose whole-pixel corners are xy[] (x | y << 16). */
void pc_ui_precise_quad(const void* packet, const float* corners);
int pc_ui_precise_lookup(const void* packet, const unsigned* xy, int count, float* corners);
/* Where magnified 2D was drawn (VRAM pixels), accumulated until written
 * as a 1024 x 512 PGM (255: drawn) by pc_ui_upscale_mask, which clears it. */
void pc_ui_upscale_note(float x0, float y0, float x1, float y1);
int pc_ui_upscale_mask(const char* path);
int pc_ui_upscale_regions(int (*out)[5], int max);
/* The CPU reference: 2x MMPX of w x h PS1 colours into dst (2w x 2h). */
void pc_ui_mmpx2x(const uint32_t* src, int w, int h, uint32_t* dst);

#ifdef __cplusplus
}
#endif
#endif
