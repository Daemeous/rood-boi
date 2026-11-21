/* The GPU's line packets (GP0 0x40-0x5F), layouts as PsyQ LINE_F/G2/3/4 (libgpu.h): each segment is handed to the renderer
 * (pc_hwc_line); nothing is drawn here. */
static void line_segment(uint32_t a, uint32_t b, unsigned ca, unsigned cb, int semi)
{
    int x0 = (int32_t)(a << 21) >> 21, y0 = (int32_t)(a << 5) >> 21;
    int x1 = (int32_t)(b << 21) >> 21, y1 = (int32_t)(b << 5) >> 21;
    int dx = x1 - x0, dy = y1 - y0;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax >= 1024 || ay >= 512)
        return;
    vram_note_drawn((x0 < x1 ? x0 : x1) + draw_x > clip_x ? (x0 < x1 ? x0 : x1) + draw_x : clip_x,
        (y0 < y1 ? y0 : y1) + draw_y > clip_y ? (y0 < y1 ? y0 : y1) + draw_y : clip_y,
        (x0 > x1 ? x0 : x1) + draw_x < clip_x + clip_w - 1 ? (x0 > x1 ? x0 : x1) + draw_x : clip_x + clip_w - 1,
        (y0 > y1 ? y0 : y1) + draw_y < clip_y + clip_h - 1 ? (y0 > y1 ? y0 : y1) + draw_y : clip_y + clip_h - 1);
    int clip[4] = { clip_x, clip_y, clip_w, clip_h };
    pc_hwc_set_2d(1); /* (a line that reaches the renderer is 2D: what 3D draws is native) */
    {
        const int ends[2][2] = { { x0, y0 }, { x1, y1 } };
        if (narrowed_2d(ends, 2))
            pc_hwc_set_narrowed();
    }
    /* Widescreen: a line standing for the whole screen's width (the event film's scanlines) draws over the full view, as a
     * polygon or rectangle that wide does (pc_hw_set_bounds decides; a line is never extended, only covered). */
    pc_hwc_set_bounds((x0 < x1 ? x0 : x1) + draw_x, (y0 < y1 ? y0 : y1) + draw_y, (x0 > x1 ? x0 : x1) + draw_x + 1,
        (y0 > y1 ? y0 : y1) + draw_y + 1, 0x40u | (semi ? 2u : 0u), tpage,
        0, NULL, clip);
    pc_hwc_line(x0 + draw_x, y0 + draw_y, x1 + draw_x, y1 + draw_y, ca, cb, semi, tpage, clip,
        mask_set);
}
static unsigned line_command(uint32_t* p, unsigned n)
{
    unsigned code = p[0] >> 24;
    int gouraud = (code & 16) != 0, polyline = (code & 8) != 0;
    unsigned used = 2, color = p[0];
    if (n < (gouraud ? 4u : 3u))
        pc_unimplemented("truncated line packet");
    unsigned xy = p[1];
    for (;;) {
        unsigned next_color = color;
        if (used >= n)
            pc_unimplemented("unterminated polyline packet");
        if (polyline && (p[used] & 0xf000f000) == 0x50005000)
            return used + 1;
        if (gouraud)
            next_color = p[used++];
        if (used >= n)
            pc_unimplemented("truncated Gouraud line vertex");
        unsigned next_xy = p[used++];
        line_segment(xy, next_xy, color, next_color, code & 2);
        xy = next_xy;
        color = next_color;
        if (!polyline)
            return used;
    }
}
