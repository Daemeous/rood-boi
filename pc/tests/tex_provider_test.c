/* The texture provider's PS1 adapter (pc/src/tex_provider.c) without a window or the game: versions per cell, handles, and the decode behind them. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "tex_provider.h"
#include "vram_texels.h"

static uint16_t image[1024 * 512];
static pc_tex_versions versions;

int main(void)
{
    /* a 4-bit page at (64, 0) (tpage 1) through a CLUT at (768, 224) (clut = 224 << 6 | 48) */
    unsigned tpage = 1, clut = 224u << 6 | 48;
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 64; x++)
            image[(size_t)y * 1024 + 64 + x] = (uint16_t)(0x4321 + x + y);
    for (int i = 0; i < 16; i++)
        image[(size_t)224 * 1024 + 768 + i] = (uint16_t)(0x100 + i);
    pc_tex_written_all(&versions);
    int box[4] = { 3, 5, 6, 6 };
    pc_tex_sampling s = pc_tex_describe(&versions, tpage, clut, 0, box);
    assert(s.mode == PC_TEX_INDEXED4 && s.page.id && s.palette.id && s.page.id != s.palette.id);
    assert(pc_tex_current(&versions, s.page) && pc_tex_current(&versions, s.palette));

    /* the decode: the raw index and the colour through the palette */
    pc_tex_view view = { image, &versions };
    uint16_t out[64];
    size_t n = pc_tex_box_indices(view, &s, out, 64);
    assert(n == 8);
    for (int v = 5; v <= 6; v++)
        for (int u = 3; u <= 6; u++)
            assert(out[(v - 5) * 4 + (u - 3)] == pc_tex_index(image, tpage, u, v));
    assert(pc_tex_palette_colours(view, &s, out, 64) == 16 && out[7] == 0x107);
    assert(pc_tex_texel(view, &s, 3, 5) == (unsigned)(0x100 + pc_tex_index(image, tpage, 3, 5)));

    /* a CLUT commit beside the page moves the palette, not the page */
    pc_tex_written(&versions, 768, 224, 256, 14);
    assert(pc_tex_current(&versions, s.page) && !pc_tex_current(&versions, s.palette));
    s = pc_tex_describe(&versions, tpage, clut, 0, box);
    assert(pc_tex_current(&versions, s.palette));

    /* an upload inside the page moves the page and only the pages that cover it */
    pc_tex_sampling other = pc_tex_describe(&versions, 2, clut, 0, box); /* page x 128, same row */
    pc_tex_written(&versions, 70, 20, 4, 4);
    assert(!pc_tex_current(&versions, s.page) && pc_tex_current(&versions, other.page) && pc_tex_current(&versions, s.palette));
    s = pc_tex_describe(&versions, tpage, clut, 0, box);

    /* an 8-bit page at the same x covers twice the width: a write at x 150 (inside it, outside the 4-bit page) moves it */
    pc_tex_sampling wide = pc_tex_describe(&versions, 1 | 1 << 7, clut, 0, box);
    assert(wide.mode == PC_TEX_INDEXED8 && wide.page.id != s.page.id);
    pc_tex_written(&versions, 150, 0, 2, 2);
    assert(!pc_tex_current(&versions, wide.page) && pc_tex_current(&versions, s.page));

    /* a box's version follows only the texels it covers: a write in the page outside the box leaves the box current */
    s = pc_tex_describe(&versions, tpage, clut, 0, box);
    assert(pc_tex_box_current(&versions, &s));
    pc_tex_written(&versions, 64 + 20, 100, 4, 4); /* the page, far from rows 5..6 and u 3..6 */
    assert(pc_tex_box_current(&versions, &s) && !pc_tex_current(&versions, s.page));
    pc_tex_written(&versions, 64, 5, 2, 1); /* under u 3..6 (x 64 + 0..1) */
    assert(!pc_tex_box_current(&versions, &s));
    s = pc_tex_describe(&versions, tpage, clut, 0, box);

    /* a 15-bit page has no palette */
    pc_tex_sampling direct = pc_tex_describe(&versions, 1 | 2 << 7, clut, 0, box);
    assert(direct.mode == PC_TEX_DIRECT16 && direct.palette.id == 0);

    /* a write wrapping the right edge moves the cells on both sides */
    pc_tex_sampling edge = pc_tex_describe(&versions, 15 | 2 << 7, 0, 0, box); /* 15-bit page at x 960, 256 wide: wraps to x 0..191 */
    pc_tex_sampling left = pc_tex_describe(&versions, 0, clut, 0, box); /* 4-bit page at x 0..63 */
    pc_tex_sampling far = pc_tex_describe(&versions, 8, clut, 0, box);  /* 4-bit page at x 512 */
    pc_tex_written(&versions, 1020, 0, 8, 1);
    assert(!pc_tex_current(&versions, edge.page) && !pc_tex_current(&versions, left.page) && pc_tex_current(&versions, far.page));

    /* a resync moves everything */
    pc_tex_sampling all = pc_tex_describe(&versions, tpage, clut, 0, box);
    pc_tex_written_all(&versions);
    assert(!pc_tex_current(&versions, all.page) && !pc_tex_current(&versions, all.palette));
    pc_tex_handle none = { 0, 0 };
    assert(!pc_tex_current(&versions, none));
    puts("tex_provider_test: ok");
    return 0;
}
