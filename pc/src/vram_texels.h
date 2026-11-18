/* How a reader reads the pixels of a VRAM image (T5-1, pc/docs/plans/2026-11-17-vram.md): the one place that knows the array's shape (1024 halfwords by
 * 512 rows, coordinates wrapped as the hardware wraps them). The readers (ui_text, ui_upscale, ui_emotes, button_prompts, button_roles, emissive,
 * environment, ptgi, screen_owner) take an image `v` (the game's mirror or the renderer's copy: they are not told which) and ask for a texel, a row
 * or a CLUT row here; none indexes it by hand. The lint rule `vram_direct` counts the hand indexing left. */
#ifndef PC_VRAM_TEXELS_H
#define PC_VRAM_TEXELS_H
/* (no system headers: the game-side units that read VRAM are compiled without them) */
typedef __UINT16_TYPE__ pc_vram_word;
typedef __SIZE_TYPE__ pc_vram_size;

/* The halfword at (x, y). */
static inline unsigned pc_vram_at(const pc_vram_word* v, int x, int y)
{
    return v[(pc_vram_size)(y & 511) * 1024 + (pc_vram_size)(x & 1023)];
}
/* The first halfword of row y; the caller indexes along it. */
static inline const pc_vram_word* pc_vram_row(const pc_vram_word* v, int y)
{
    return v + (pc_vram_size)(y & 511) * 1024;
}
/* The first entry of the CLUT row a CLUT word names (x in 16s in the low 6 bits, y above). */
static inline const pc_vram_word* pc_vram_clut(const pc_vram_word* v, unsigned clut)
{
    return pc_vram_row(v, (int)(clut >> 6 & 511)) + (clut & 63) * 16;
}
#endif
