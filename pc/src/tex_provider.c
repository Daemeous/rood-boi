/* The PS1 adapter of the texture and palette provider (tex_provider.h). */
#include "tex_provider.h"
#include "vram_texels.h"

enum { PAGE_ID = 0x100000, PALETTE_ID = 0x200000 };
static unsigned tpage_of(const pc_tex_sampling* s);

static inline uint32_t* cell_at(pc_tex_versions* t, int cx, int cy) { return &t->cell[cy & (PC_TEX_CELLS_Y - 1)][cx & (PC_TEX_CELLS_X - 1)]; }

void pc_tex_written(pc_tex_versions* t, int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    uint32_t version = ++t->epoch;
    int x0 = (x & 1023) >> 6, y0 = (y & 511) >> 4;
    int x1 = ((x + w - 1) & 1023) >> 6, y1 = ((y + h - 1) & 511) >> 4;
    int nx = w >= 1024 ? PC_TEX_CELLS_X : ((x1 - x0) & (PC_TEX_CELLS_X - 1)) + 1; /* (a rectangle that wraps covers the cells across the edge) */
    int ny = h >= 512 ? PC_TEX_CELLS_Y : ((y1 - y0) & (PC_TEX_CELLS_Y - 1)) + 1;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++)
            *cell_at(t, x0 + i, y0 + j) = version;
}
void pc_tex_written_all(pc_tex_versions* t)
{
    uint32_t version = ++t->epoch;
    for (int j = 0; j < PC_TEX_CELLS_Y; j++)
        for (int i = 0; i < PC_TEX_CELLS_X; i++)
            t->cell[j][i] = version;
}

/* The highest version of the cells an area covers. */
static uint32_t area_version(const pc_tex_versions* t, int x, int y, int w, int h)
{
    uint32_t top = 0;
    int x0 = (x & 1023) >> 6, y0 = (y & 511) >> 4;
    int x1 = ((x + w - 1) & 1023) >> 6, y1 = ((y + h - 1) & 511) >> 4;
    int nx = ((x1 - x0) & (PC_TEX_CELLS_X - 1)) + 1, ny = ((y1 - y0) & (PC_TEX_CELLS_Y - 1)) + 1;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            uint32_t v = t->cell[(y0 + j) & (PC_TEX_CELLS_Y - 1)][(x0 + i) & (PC_TEX_CELLS_X - 1)];
            top = v > top ? v : top;
        }
    return top;
}
static pc_tex_mode mode_of(unsigned tpage)
{
    int depth = pc_tex_depth(tpage);
    return depth == 0 ? PC_TEX_INDEXED4 : depth == 1 ? PC_TEX_INDEXED8 : PC_TEX_DIRECT16;
}
/* The page's extent in halfwords: 64, 128 or 256 wide, 256 high. */
static int page_width(pc_tex_mode mode) { return mode == PC_TEX_INDEXED4 ? 64 : mode == PC_TEX_INDEXED8 ? 128 : 256; }
static int palette_width(pc_tex_mode mode) { return mode == PC_TEX_INDEXED4 ? 16 : 256; }

pc_tex_handle pc_tex_page_handle(const pc_tex_versions* t, unsigned tpage)
{
    pc_tex_mode mode = mode_of(tpage);
    pc_tex_handle h = { PAGE_ID | (uint32_t)mode << 8 | (tpage & 0x1f), 0 }; /* x (4 bits) and y (1 bit) of the page, and its mode */
    h.version = area_version(t, pc_tex_page_x(tpage), pc_tex_page_y(tpage), page_width(mode), 256);
    return h;
}
pc_tex_handle pc_tex_palette_handle(const pc_tex_versions* t, unsigned tpage, unsigned clut)
{
    pc_tex_mode mode = mode_of(tpage);
    pc_tex_handle none = { 0, 0 };
    if (mode == PC_TEX_DIRECT16)
        return none;
    pc_tex_handle h = { PALETTE_ID | (uint32_t)(mode == PC_TEX_INDEXED8) << 16 | (clut & 0x7fff), 0 };
    h.version = area_version(t, (int)(clut & 63) * 16, (int)(clut >> 6 & 511), palette_width(mode), 1);
    return h;
}
/* The version of the part of the page a box covers (its texels' halfwords: a 4-bit texel is a quarter halfword, an 8-bit one a half). */
static uint32_t box_version(const pc_tex_versions* t, unsigned tpage, pc_tex_mode mode, const int box[4])
{
    int shift = mode == PC_TEX_INDEXED4 ? 2 : mode == PC_TEX_INDEXED8 ? 1 : 0;
    int u0 = box[0] & 255, u1 = box[2] & 255, v0 = box[1] & 255, v1 = box[3] & 255;
    if (u1 < u0 || v1 < v0) /* (a box that wraps the page: the whole page) */
        u0 = v0 = 0, u1 = v1 = 255;
    int x0 = pc_tex_page_x(tpage) + (u0 >> shift), x1 = pc_tex_page_x(tpage) + (u1 >> shift);
    return area_version(t, x0, pc_tex_page_y(tpage) + v0, x1 - x0 + 1, v1 - v0 + 1);
}
pc_tex_sampling pc_tex_describe(const pc_tex_versions* t, unsigned tpage, unsigned clut, unsigned window, const int uv_box[4])
{
    static const pc_tex_versions none; /* (geometry only: versions of 0) */
    if (!t)
        t = &none;
    pc_tex_sampling s;
    s.mode = mode_of(tpage);
    s.page = pc_tex_page_handle(t, tpage);
    s.palette = pc_tex_palette_handle(t, tpage, clut);
    for (int i = 0; i < 4; i++)
        s.box[i] = uv_box[i];
    s.window = window;
    s.box_version = box_version(t, tpage, s.mode, uv_box);
    return s;
}
int pc_tex_reach(const pc_tex_sampling* s, int rects[2][4])
{
    unsigned tpage = tpage_of(s);
    int shift = s->mode == PC_TEX_INDEXED4 ? 2 : s->mode == PC_TEX_INDEXED8 ? 1 : 0;
    int tx = pc_tex_page_x(tpage), ty = pc_tex_page_y(tpage);
    rects[0][0] = tx + (s->box[0] >> shift), rects[0][1] = ty + s->box[1];
    rects[0][2] = tx + (s->box[2] >> shift), rects[0][3] = ty + s->box[3];
    if (s->mode == PC_TEX_DIRECT16)
        return 1;
    unsigned clut = s->palette.id & 0x7fff;
    rects[1][0] = (int)(clut & 63) * 16, rects[1][1] = (int)(clut >> 6 & 511);
    rects[1][2] = rects[1][0] + palette_width(s->mode) - 1, rects[1][3] = rects[1][1];
    return 2;
}
int pc_tex_box_current(const pc_tex_versions* t, const pc_tex_sampling* s)
{
    return box_version(t, (s->page.id & 0x1f) | (unsigned)s->mode << 7, s->mode, s->box) == s->box_version;
}
/* The area a handle names, from its id. */
static int handle_area(pc_tex_handle h, int* x, int* y, int* w, int* hh)
{
    if ((h.id & 0xf00000) == PAGE_ID) {
        pc_tex_mode mode = (pc_tex_mode)(h.id >> 8 & 3);
        *x = (int)(h.id & 15) * 64, *y = (int)(h.id >> 4 & 1) * 256, *w = page_width(mode), *hh = 256;
        return 1;
    }
    if ((h.id & 0xf00000) == PALETTE_ID) {
        unsigned clut = h.id & 0x7fff;
        *x = (int)(clut & 63) * 16, *y = (int)(clut >> 6 & 511), *w = h.id >> 16 & 1 ? 256 : 16, *hh = 1;
        return 1;
    }
    return 0;
}
int pc_tex_current(const pc_tex_versions* t, pc_tex_handle handle)
{
    int x, y, w, h;
    return handle_area(handle, &x, &y, &w, &h) && area_version(t, x, y, w, h) == handle.version;
}

/* A page's raw texel needs the page's tpage word: rebuilt from its id (x, y and mode; the depth bits). */
static unsigned tpage_of(const pc_tex_sampling* s)
{
    return (s->page.id & 0x1f) | (unsigned)s->mode << 7;
}
size_t pc_tex_box_indices(pc_tex_view view, const pc_tex_sampling* s, uint16_t* out, size_t max)
{
    unsigned tpage = tpage_of(s);
    size_t n = 0;
    for (int v = s->box[1]; v <= s->box[3]; v++)
        for (int u = s->box[0]; u <= s->box[2] && n < max; u++)
            out[n++] = (uint16_t)pc_tex_index(view.image, tpage, u & 255, v & 255);
    return n;
}
size_t pc_tex_palette_colours(pc_tex_view view, const pc_tex_sampling* s, uint16_t* out, size_t max)
{
    if (s->mode == PC_TEX_DIRECT16)
        return 0;
    unsigned clut = s->palette.id & 0x7fff;
    int cx = (int)(clut & 63) * 16, cy = (int)(clut >> 6 & 511);
    size_t count = (size_t)palette_width(s->mode), n = 0;
    for (; n < count && n < max; n++)
        out[n] = (uint16_t)pc_vram_at(view.image, cx + (int)n, cy); /* (the row wraps at the edge) */
    return n;
}
unsigned pc_tex_texel(pc_tex_view view, const pc_tex_sampling* s, int u, int v)
{
    unsigned tpage = tpage_of(s);
    unsigned index = pc_tex_index(view.image, tpage, u & 255, v & 255);
    unsigned clut = s->palette.id & 0x7fff;
    return s->mode == PC_TEX_DIRECT16 ? index : pc_vram_at(view.image, (int)(clut & 63) * 16 + (int)index, (int)(clut >> 6 & 511));
}
