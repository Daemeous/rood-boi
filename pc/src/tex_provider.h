/* The texture and palette provider (T5-7, pc/docs/plans/2026-11-17-vram.md "interface sketch"): textures as handles with versions, and the PS1
 * adapter that makes them from the words a primitive carries (tpage, clut, uv box, window) over a VRAM image. The renderer and the content systems
 * (materials, emissive, UI text) ask here for what a draw samples and for its content; none decodes VRAM by hand (vram_texels.h is this module's
 * internals). Another game implements the same handles: Kagero over native textures (versions never move), a PS2 port over GS memory.
 *
 * A VRAM image comes with its versions (pc_tex_versions): one counter per 64 x 16 halfword cell, bumped by every write that covers the cell (upload, fill,
 * copy, read-back, whole-image resync). A page's or palette's version is the highest of the cells it lies in, so a CLUT commit beside a page does not move
 * the page. The image and its versions are kept together by whoever owns the image (the renderer's copy, a published copy for the game thread). */
#ifndef PC_TEX_PROVIDER_H
#define PC_TEX_PROVIDER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PC_TEX_CELLS_X 16 /* 1024 / 64 */
#define PC_TEX_CELLS_Y 32 /* 512 / 16 */
typedef struct {
    uint32_t cell[PC_TEX_CELLS_Y][PC_TEX_CELLS_X];
    uint32_t epoch; /* the last version handed out: a write's version is epoch + 1 */
} pc_tex_versions;

typedef struct {
    uint32_t id;      /* opaque and stable while the resource exists; 0 is none */
    uint32_t version; /* bumped when the contents change */
} pc_tex_handle;
typedef enum { PC_TEX_INDEXED4, PC_TEX_INDEXED8, PC_TEX_DIRECT16 } pc_tex_mode;
typedef struct {
    pc_tex_handle page;
    pc_tex_handle palette; /* none (id 0) for DIRECT16 */
    pc_tex_mode mode;
    int box[4];        /* u0, v0, u1, v1, inclusive, in the page */
    uint32_t box_version; /* the version of the part of the page the box covers: what a cache of the box's content is keyed on (the page's own
                             version moves with every write anywhere in the page, a CLUT row that lies under it included) */
    unsigned window;   /* the texture window word (GP0 E2: mask and offset), 0 for none */
} pc_tex_sampling;
/* A VRAM image with its versions (neither is owned here). */
typedef struct {
    const uint16_t* image; /* 1024 x 512 halfwords */
    const pc_tex_versions* versions;
} pc_tex_view;

/* Writes: the rectangle (x, y, w, h), wrapping at the edges as the hardware does, changed; or all of it. */
void pc_tex_written(pc_tex_versions* versions, int x, int y, int w, int h);
void pc_tex_written_all(pc_tex_versions* versions);

pc_tex_handle pc_tex_page_handle(const pc_tex_versions* versions, unsigned tpage);
pc_tex_handle pc_tex_palette_handle(const pc_tex_versions* versions, unsigned tpage, unsigned clut);
/* What one draw samples, from the words its primitive carries. */
pc_tex_sampling pc_tex_describe(const pc_tex_versions* versions, unsigned tpage, unsigned clut, unsigned window, const int uv_box[4]);
/* Whether the handle still names the resource's current contents. */
int pc_tex_current(const pc_tex_versions* versions, pc_tex_handle handle);
/* Whether the texels under the sampling's box are as they were when it was described. */
int pc_tex_box_current(const pc_tex_versions* versions, const pc_tex_sampling* sampling);

/* Content, for the systems that recognise and light an image. The page's raw texels of the box, row by row (CLUT indices for 4- and 8-bit pages, colours for
 * 16-bit), and the palette's entries (16 or 256 colours; none for DIRECT16). Return the count written (at most `max`). */
size_t pc_tex_box_indices(pc_tex_view view, const pc_tex_sampling* sampling, uint16_t* out, size_t max);
size_t pc_tex_palette_colours(pc_tex_view view, const pc_tex_sampling* sampling, uint16_t* out, size_t max);
/* The colour of texel (u, v) of the sampling's page (through its palette), as the hardware reads it. */
unsigned pc_tex_texel(pc_tex_view view, const pc_tex_sampling* sampling, int u, int v);
#ifdef __cplusplus
}
#endif
#endif
