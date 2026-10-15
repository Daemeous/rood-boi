/* Emote bubbles (ui_bubbles.h; README "2D / UI").
 *
 * BATTLE.PRG func_80095C18 draws the small bubbles over actors (NPCs in
 * scenes, enemies): a balloon image (a quad, mirrored by the side the actor
 * stands on) from the battle page (448, 256), CLUT (992, 220), with one to
 * three dots, a '!' or a heart in it, or a '?' alone; the speech balloon,
 * a spiky shout balloon or surprise strokes. As 2D regions
 * of that page and CLUT they come through the 2D atlas (ui_upscale.c) and,
 * when they still hold the game's images (a hash of their CLUT indices),
 * are drawn again as distance texels (ui_text.h):
 *
 * - balloons and strokes (ui_bubbles vector), as the talk bubbles: one
 *   smooth shape, the image's silhouette smoothed (its pixel steps rounded
 *   off, a Gaussian of SMOOTH texels thresholded half way) with an
 *   anti-aliased outline one PS1 pixel wide in the image's own outline
 *   colour, the inside the image's own light fill (its paper texture,
 *   nearest; its dark pixel outline replaced by the fill next to it);
 * - the dots (ui_text font): discs of the dots' dark ink's size, place and
 *   colour (the light texels around it, the balloon's, left to the balloon);
 * - '!' and '?' (ui_text font): the dialogue style's font (the talk
 *   bubbles' lettering), fitted to the original's ink box: a dark mark in
 *   its ink's colour, a light one (the '?' alone) inside its dark outline.
 * The heart is a pictograph and stays the game's. */
#include "ui_bubbles.h"
#include "ui_text.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BALLOON, DOT, MARK };
typedef struct {
    int u0, v0, u1, v1; /* the image's texels (inclusive) */
    int kind;
    unsigned codepoint; /* MARK */
    uint32_t hash;      /* of its CLUT indices: the game's image */
} emote_image;
static const emote_image images[] = {
    { 64, 16, 87, 38, BALLOON, 0, 0x92b69f94u },   /* the speech balloon */
    { 121, 17, 150, 46, BALLOON, 0, 0x1dc2f17fu },  /* the shout balloon */
    { 160, 17, 179, 29, BALLOON, 0, 0x645cd925u },  /* the surprise strokes */
    { 90, 18, 94, 22, DOT, 0, 0x5f278e5du },
    { 152, 17, 158, 28, MARK, '!', 0xb67c3a29u },
    { 183, 17, 189, 28, MARK, '?', 0x4fb4be08u },
};
#define IMAGES (int)(sizeof images / sizeof images[0])
#define PAGE_X 448
#define PAGE_Y 256
#define CLUT_WORD ((220u << 6) | (992u >> 4))
#define SMOOTH 0.6f /* texels: the silhouette's smoothing */

static int luma555(uint16_t c) { return (c & 31) * 299 + (c >> 5 & 31) * 587 + (c >> 10 & 31) * 114; }
static const uint16_t* clut_of(const uint16_t* vram) { return vram + 220 * 1024 + 992; }
static int index_at(const uint16_t* vram, int u, int v)
{
    return (vram[((PAGE_Y + v) & 511) * 1024 + ((PAGE_X + (u >> 2)) & 1023)] >> ((u & 3) * 4)) & 15;
}
static uint32_t image_hash(const uint16_t* vram, const emote_image* e)
{
    uint32_t h = 2166136261u;
    for (int v = e->v0; v <= e->v1; v++)
        for (int u = e->u0; u <= e->u1; u++)
            h = (h ^ (uint32_t)index_at(vram, u, v)) * 16777619u;
    return h;
}

/* The image a region is, or NULL (another page, CLUT or rectangle, the
 * option off, or the page holds something else now). */
static const emote_image* find(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1)
{
    if (!vram || (tpage & 0x19f) != 0x17 || (clut & 0x7fff) != CLUT_WORD)
        return NULL; /* the battle page, 4-bit */
    static int debug = -1;
    if (debug < 0)
        debug = getenv("ROOD_UI_DEBUG") ? atoi(getenv("ROOD_UI_DEBUG")) : 0;
    for (int i = 0; i < IMAGES; i++) {
        const emote_image* e = &images[i];
        if (e->u0 != u0 || e->v0 != v0 || e->u1 != u1 || e->v1 != v1)
            continue;
        int on = e->kind == BALLOON ? pc_ui_bubbles == PC_UI_BUBBLES_VECTOR : pc_ui_text == PC_UI_TEXT_FONT;
        if (!on)
            return NULL;
        uint32_t h = image_hash(vram, e);
        if (debug >= 5) { /* ROOD_UI_DEBUG=5: the images and their hashes */
            static uint32_t shown[IMAGES];
            if (shown[i] != h) {
                shown[i] = h;
                fprintf(stderr, "[ui] emote image %d,%d..%d,%d hash 0x%08xu\n", u0, v0, u1, v1, h);
                for (int v = v0; v <= v1; v++) {
                    char row[64];
                    int n = 0;
                    for (int u = u0; u <= u1 && n < 63; u++)
                        row[n++] = "0123456789abcdef"[index_at(vram, u, v)];
                    row[n] = 0;
                    fprintf(stderr, "[ui]   %s\n", row);
                }
            }
        }
        return h == e->hash ? e : NULL;
    }
    return NULL;
}

int pc_ui_emote_classify(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    uint64_t* key)
{
    const emote_image* e = find(vram, tpage, clut, u0, v0, u1, v1);
    if (!e)
        return 0;
    *key = (uint64_t)pc_ui_text_generation << 40 ^ (uint64_t)0xe307 << 20 ^ (uint64_t)(e - images + 1)
         ^ (uint64_t)(pc_ui_bubble_outline * 64.0f) << 8;
    return 1;
}

/* ---- balloons ---- */

/* Signed distance (texels of a w x h grid, positive inside) to the edge of
 * `inside`: the nearest cell of the other kind (8SSEDT, two passes), less
 * half a cell. */
static void distance_field(const unsigned char* inside, int w, int h, float* out)
{
    typedef struct {
        short dx, dy;
    } offset;
    offset* g = malloc(sizeof *g * (size_t)w * h);
    if (!g)
        return;
    for (int pass = 0; pass < 2; pass++) {
        /* pass 0: distance to the nearest outside cell (for inside cells); 1: to the nearest inside cell */
        const short far = 10000;
        for (int i = 0; i < w * h; i++) {
            int target = pass == 0 ? !inside[i] : inside[i];
            g[i].dx = g[i].dy = target ? 0 : far;
        }
#define DIST2(o) ((int)(o).dx * (o).dx + (int)(o).dy * (o).dy)
#define TRY(x, y, ox, oy)                                                                                            \
    do {                                                                                                             \
        int nx = (x) + (ox), ny = (y) + (oy);                                                                        \
        if (nx >= 0 && ny >= 0 && nx < w && ny < h) {                                                                \
            offset c = g[ny * w + nx];                                                                               \
            if (c.dx != far) {                                                                                       \
                c.dx = (short)(c.dx + (ox)), c.dy = (short)(c.dy + (oy));                                            \
                if (DIST2(c) < DIST2(g[(y) * w + (x)]))                                                              \
                    g[(y) * w + (x)] = c;                                                                            \
            }                                                                                                        \
        }                                                                                                            \
    } while (0)
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                TRY(x, y, -1, 0);
                TRY(x, y, 0, -1);
                TRY(x, y, -1, -1);
                TRY(x, y, 1, -1);
            }
            for (int x = w - 1; x >= 0; x--)
                TRY(x, y, 1, 0);
        }
        for (int y = h - 1; y >= 0; y--) {
            for (int x = w - 1; x >= 0; x--) {
                TRY(x, y, 1, 0);
                TRY(x, y, 0, 1);
                TRY(x, y, -1, 1);
                TRY(x, y, 1, 1);
            }
            for (int x = 0; x < w; x++)
                TRY(x, y, -1, 0);
        }
#undef TRY
#undef DIST2
        for (int i = 0; i < w * h; i++) {
            float d = g[i].dx == far ? 1e4f : sqrtf((float)g[i].dx * g[i].dx + (float)g[i].dy * g[i].dy) - 0.5f;
            if (pass == 0 && inside[i])
                out[i] = d;
            else if (pass == 1 && !inside[i])
                out[i] = -d;
        }
    }
    free(g);
}

static int render_balloon(const uint16_t* vram, const emote_image* e, int factor, uint32_t* out)
{
    int tw = e->u1 - e->u0 + 1, th = e->v1 - e->v0 + 1;
    if (tw > 64 || th > 64)
        return 0;
    int index[64][64], mask[64][64];
    int counts[16] = { 0 }, edge_counts[16] = { 0 };
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            index[y][x] = index_at(vram, e->u0 + x, e->v0 + y);
            mask[y][x] = index[y][x] != 0;
        }
    /* The outline's colour: the commonest index along the silhouette. */
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            if (!mask[y][x])
                continue;
            int edge = x == 0 || y == 0 || x == tw - 1 || y == th - 1 || !mask[y][x - 1] || !mask[y][x + 1]
                    || !mask[y - 1][x] || !mask[y + 1][x];
            ++(edge ? edge_counts : counts)[index[y][x]];
        }
    int outline = 1;
    for (int i = 1; i < 16; i++)
        outline = edge_counts[i] > edge_counts[outline] ? i : outline;
    /* The fill nearest each texel: its light colours (not the outline or the
     * dark band along it: the shape draws its own outline). */
    const uint16_t* lut = clut_of(vram);
    int lo_luma = 1 << 30, hi_luma = 0;
    for (int i = 1; i < 16; i++)
        if (counts[i] + edge_counts[i])
            lo_luma = luma555(lut[i]) < lo_luma ? luma555(lut[i]) : lo_luma,
            hi_luma = luma555(lut[i]) > hi_luma ? luma555(lut[i]) : hi_luma;
    int middle = (lo_luma + hi_luma) / 2;
    int fill[64][64];
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            int best = -1, best_d = 1 << 30;
            for (int yy = 0; yy < th; yy++)
                for (int xx = 0; xx < tw; xx++) {
                    int i = index[yy][xx];
                    if (!i || i == outline || luma555(lut[i]) < middle)
                        continue;
                    int d = (xx - x) * (xx - x) + (yy - y) * (yy - y);
                    if (d < best_d)
                        best_d = d, best = i;
                }
            fill[y][x] = best > 0 ? best : outline;
        }
    /* The silhouette smoothed, at the atlas's resolution, in a grid a texel
     * wider each way (outside: the image's edge is an edge of the shape). */
    int pad = factor, w = tw * factor, h = th * factor, gw = w + 2 * pad, gh = h + 2 * pad;
    unsigned char* inside = malloc((size_t)gw * gh);
    float* distance = malloc(sizeof *distance * (size_t)gw * gh);
    if (!inside || !distance) {
        free(inside);
        free(distance);
        return 0;
    }
    float k = 1.0f / (2.0f * SMOOTH * SMOOTH);
    for (int y = 0; y < gh; y++)
        for (int x = 0; x < gw; x++) {
            float px = (x - pad + 0.5f) / factor, py = (y - pad + 0.5f) / factor, sum = 0, weight = 0;
            int in_box = x >= pad && y >= pad && x < pad + w && y < pad + h;
            for (int ty = (int)floorf(py) - 2; ty <= (int)floorf(py) + 2 && in_box; ty++)
                for (int tx = (int)floorf(px) - 2; tx <= (int)floorf(px) + 2; tx++) {
                    float dx = tx + 0.5f - px, dy = ty + 0.5f - py, wgt = expf(-(dx * dx + dy * dy) * k);
                    int m = tx >= 0 && ty >= 0 && tx < tw && ty < th && mask[ty][tx];
                    sum += m * wgt, weight += wgt;
                }
            inside[y * gw + x] = in_box && sum >= 0.5f * weight;
        }
    distance_field(inside, gw, gh, distance);
    /* Distance texels: the outline's outer edge on the silhouette (LABEL: the
     * outline a PS1 pixel wide outside the fill's edge). */
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float d = distance[(y + pad) * gw + x + pad] / factor - 1.0f;
            out[y * w + x] = pc_ui_dist_texel(d, PC_UI_DIST_LABEL, outline, fill[y / factor][x / factor], 1);
        }
    free(inside);
    free(distance);
    return 1;
}

/* ---- dots and marks ---- */

/* The middle of the luma of the image's colours (dark ink below it). */
static int luma_middle(const uint16_t* vram, const emote_image* e)
{
    const uint16_t* lut = clut_of(vram);
    int lo = 1 << 30, hi = 0;
    for (int v = e->v0; v <= e->v1; v++)
        for (int u = e->u0; u <= e->u1; u++) {
            int i = index_at(vram, u, v);
            if (i)
                lo = luma555(lut[i]) < lo ? luma555(lut[i]) : lo, hi = luma555(lut[i]) > hi ? luma555(lut[i]) : hi;
        }
    return (lo + hi) / 2;
}

static int render_dot(const uint16_t* vram, const emote_image* e, int factor, uint32_t* out)
{
    /* Its ink: each texel by how dark it is between the image's lightest and
     * darkest colours (the light ones are the balloon's fill around it); a
     * disc of that area at that centre, in the darkest colour. */
    int tw = e->u1 - e->u0 + 1, th = e->v1 - e->v0 + 1, lo = 1 << 30, hi = 0, ink = 0;
    const uint16_t* lut = clut_of(vram);
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            int i = index_at(vram, e->u0 + x, e->v0 + y), l = luma555(lut[i]);
            if (!i)
                continue;
            hi = l > hi ? l : hi;
            if (l < lo)
                lo = l, ink = i;
        }
    if (!ink || hi <= lo)
        return 0;
    float area = 0, cx = 0, cy = 0;
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            int i = index_at(vram, e->u0 + x, e->v0 + y);
            float t = i ? (float)(hi - luma555(lut[i])) / (float)(hi - lo) : 0.0f;
            area += t, cx += t * (x + 0.5f), cy += t * (y + 0.5f);
        }
    cx /= area, cy /= area;
    float r = sqrtf(area / 3.14159265f);
    int w = tw * factor, h = th * factor;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float d = r - hypotf((x + 0.5f) / factor - cx, (y + 0.5f) / factor - cy);
            out[y * w + x] = pc_ui_dist_texel(d, PC_UI_DIST_RAMP, ink, ink, 0);
        }
    return 1;
}

static int render_mark(const uint16_t* vram, const emote_image* e, int factor, uint32_t* out)
{
    int tw = e->u1 - e->u0 + 1, th = e->v1 - e->v0 + 1;
    int x0 = tw, y0 = th, x1 = -1, y1 = -1, edge_counts[16] = { 0 }, counts[16] = { 0 };
    int index[64][64];
    if (tw > 64 || th > 64)
        return 0;
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++)
            index[y][x] = index_at(vram, e->u0 + x, e->v0 + y);
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            int i = index[y][x];
            if (!i)
                continue;
            x0 = x < x0 ? x : x0, y0 = y < y0 ? y : y0, x1 = x > x1 ? x : x1, y1 = y > y1 ? y : y1;
            int edge = x == 0 || y == 0 || x == tw - 1 || y == th - 1 || !index[y][x - 1] || !index[y][x + 1]
                    || !index[y - 1][x] || !index[y + 1][x];
            ++(edge ? edge_counts : counts)[i];
        }
    if (x1 < 0)
        return 0;
    /* A light mark inside a dark rim (outlined: the rim's commonest dark
     * colour and the inside's commonest light one), or a dark mark (its
     * commonest dark colour; light texels along it are its blend with the
     * balloon's fill). */
    const uint16_t* lut = clut_of(vram);
    int middle = luma_middle(vram, e), rim_dark = 0, rim_all = 0;
    for (int i = 1; i < 16; i++) {
        rim_all += edge_counts[i];
        rim_dark += luma555(lut[i]) < middle ? edge_counts[i] : 0;
    }
    int outlined = rim_dark * 2 > rim_all, rim = 0, body = 0;
    for (int i = 1; i < 16; i++) {
        int dark = luma555(lut[i]) < middle, n = edge_counts[i] + counts[i];
        if (outlined) {
            if (dark && edge_counts[i] > (rim ? edge_counts[rim] : 0))
                rim = i;
            if (!dark && counts[i] > (body ? counts[body] : 0))
                body = i;
        } else if (dark && n > (rim ? edge_counts[rim] + counts[rim] : 0))
            rim = i;
    }
    if (!rim || (outlined && !body))
        return 0;
    int missing;
    const pc_ui_icon* glyph = pc_ui_icon_glyph(pc_ui_text_styles[PC_UI_TEXT_DIALOGUE].font, e->codepoint, &missing);
    if (!glyph)
        return 0;
    float g[4];
    pc_ui_icon_box(glyph, g);
    float gw = g[2] - g[0], gh = g[3] - g[1];
    if (gw <= 0 || gh <= 0)
        return 0;
    /* The lettering inside the ink box (inside its rim when outlined). */
    float in = outlined ? 1.0f : 0.0f;
    float bx0 = x0 + in, by0 = y0 + in, bx1 = x1 + 1 - in, by1 = y1 + 1 - in;
    float s = fminf((bx1 - bx0) / gw, (by1 - by0) / gh); /* PS1 pixels per field pixel */
    float cx = (bx0 + bx1) * 0.5f, cy = (by0 + by1) * 0.5f, fx = (g[0] + g[2]) * 0.5f, fy = (g[1] + g[3]) * 0.5f;
    int w = tw * factor, h = th * factor;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float px = (x + 0.5f) / factor, py = (y + 0.5f) / factor;
            float d = pc_ui_icon_distance(glyph, fx + (px - cx) / s, fy - (py - cy) / s) * s;
            out[y * w + x] = outlined ? pc_ui_dist_texel(d, PC_UI_DIST_LABEL, rim, body, 1)
                                      : pc_ui_dist_texel(d, PC_UI_DIST_RAMP, rim, rim, 0);
        }
    return 1;
}

static long rendered[3];
int pc_ui_emote_render(const uint16_t* vram, unsigned tpage, unsigned clut, int u0, int v0, int u1, int v1,
    int factor, uint32_t* out)
{
    const emote_image* e = find(vram, tpage, clut, u0, v0, u1, v1);
    if (!e || factor < 1 || factor > 8)
        return 0;
    int ok = e->kind == BALLOON ? render_balloon(vram, e, factor, out)
           : e->kind == DOT     ? render_dot(vram, e, factor, out)
                                : render_mark(vram, e, factor, out);
    rendered[e->kind] += ok;
    return ok;
}
void pc_ui_emote_stats(long* balloons, long* dots, long* marks)
{
    *balloons = rendered[BALLOON], *dots = rendered[DOT], *marks = rendered[MARK];
}
