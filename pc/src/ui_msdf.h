/* ui_text's multi-channel signed distance fields (msdfgen core, pc/third_party/msdfgen; setting ui_text_field msdf).
 *
 * A glyph's outline (stb_truetype's shape) as a field of three channels whose median is the signed distance to the
 * outline: bilinear interpolation of the channels first, the median after, keeps a corner where a single-channel field
 * rounds it. The bytes are laid out and scaled as stbtt_GetGlyphSDF's (the same pixels, the same edge value, the same
 * distance per step), three to a pixel: the channels' median, taken in ui_text.c's distance_at, replaces its one byte. */
#ifndef ROOD_UI_MSDF_H
#define ROOD_UI_MSDF_H
#ifdef __cplusplus
extern "C" {
#endif
/* An outline as stb_truetype's stbtt_GetGlyphShape gives it (its stbtt_vertex, the same layout: stb_truetype is compiled into
 * ui_text.c alone, static): types 1 move, 2 line, 3 quadratic curve (cx, cy), 4 cubic curve (cx, cy, cx1, cy1). */
typedef struct {
    short x, y, cx, cy, cx1, cy1;
    unsigned char type, padding;
} pc_msdf_vertex;
/* The glyph's field in the pixels stbtt_GetGlyphSDF would make: its box ix0, iy0 .. ix1, iy1 (stbtt_GetGlyphBitmapBoxSubpixel
 * at `scale`, padding added: w = ix1 - ix0, h = iy1 - iy0) and the arguments after it as that function's. Returns w * h * 3
 * bytes (red, green, blue of each pixel, rows top down as the SDF's; free() them) or NULL (an empty glyph, a shape the
 * generator cannot take). */
unsigned char* pc_msdf_glyph(const pc_msdf_vertex* vertices, int count, float scale, int ix0, int iy0, int ix1, int iy1,
    int padding, unsigned char onedge, float pixel_dist_scale);
#ifdef __cplusplus
}
#endif
#endif
