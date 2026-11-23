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

/* The texture seam (T5-7 prelude): how a texel of a texture page is found from the words a primitive carries. The readers that recognise,
 * hash, light or upscale an image ask here for a texel of (tpage, clut, u, v) and never decode a page by hand; the day the renderer owns
 * the pages as resources, this is the one place that changes. `depth` is the tpage word's bits 7-8 (0: 4-bit, 1: 8-bit, 2: 15-bit), (tx, ty)
 * the page's origin in VRAM (x in halfwords). The index is the CLUT entry the page holds (4 and 8 bits) or the colour itself (15 bits). */
static inline unsigned pc_tex_index_at(const pc_vram_word* v, int depth, int tx, int ty, int u, int row)
{
    if (depth == 0)
        return (pc_vram_at(v, tx + (u >> 2), ty + row) >> ((u & 3) * 4)) & 15;
    if (depth == 1)
        return (pc_vram_at(v, tx + (u >> 1), ty + row) >> ((u & 1) * 8)) & 255;
    return pc_vram_at(v, tx + u, ty + row);
}
static inline int pc_tex_depth(unsigned tpage) { return (int)(tpage >> 7 & 3); }
static inline int pc_tex_page_x(unsigned tpage) { return (int)(tpage & 15) * 64; }
static inline int pc_tex_page_y(unsigned tpage) { return (int)(tpage >> 4 & 1) * 256; }
/* The raw texel (u, row) of the page a tpage word names. */
static inline unsigned pc_tex_index(const pc_vram_word* v, unsigned tpage, int u, int row)
{
    return pc_tex_index_at(v, pc_tex_depth(tpage), pc_tex_page_x(tpage), pc_tex_page_y(tpage), u, row);
}
/* Its colour: through the CLUT a clut word names for 4- and 8-bit pages, as is for 15-bit ones. */
static inline unsigned pc_tex_colour(const pc_vram_word* v, unsigned tpage, unsigned clut, int u, int row)
{
    unsigned index = pc_tex_index(v, tpage, u, row);
    return pc_tex_depth(tpage) >= 2 ? index : pc_vram_at(v, (int)(clut & 63) * 16 + (int)index, (int)(clut >> 6 & 511));
}
#endif
