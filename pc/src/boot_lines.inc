/* Native diagnostic line rasterizer. Command layouts follow PsyQ LINE_F/G2/3/4
 * (libgpu.h); pixel-perfect PS1 edge/dither behavior is not yet claimed. */
static void line_segment(uint32_t a, uint32_t b, unsigned ca, unsigned cb, int semi)
{
    int x0 = (int32_t)(a << 21) >> 21, y0 = (int32_t)(a << 5) >> 21;
    int x1 = (int32_t)(b << 21) >> 21, y1 = (int32_t)(b << 5) >> 21;
    int dx = x1 - x0, dy = y1 - y0;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax >= 1024 || ay >= 512)
        return;
    int steps = ax > ay ? ax : ay;
    {
        int clip[4] = { clip_x, clip_y, clip_w, clip_h };
        pc_hw_set_2d(!pc_gte_log || !pc_gte_projected(x0, y0) || !pc_gte_projected(x1, y1));
        {
            const int ends[2][2] = { { x0, y0 }, { x1, y1 } };
            if (narrowed_2d(ends, 2))
                pc_hw_set_narrowed();
        }
        pc_hw_line(x0 + draw_x, y0 + draw_y, x1 + draw_x, y1 + draw_y, ca, cb, semi, tpage, clip,
            mask_set);
        pc_hw_mark((x0 < x1 ? x0 : x1) + draw_x, (y0 < y1 ? y0 : y1) + draw_y,
            (x0 > x1 ? x0 : x1) + draw_x, (y0 > y1 ? y0 : y1) + draw_y);
    }
    for (int i = 0; i <= steps; ++i) {
        int x = x0, y = y0;
        unsigned color = 0;
        if (steps) {
            x += (dx * i + (dx < 0 ? -steps / 2 : steps / 2)) / steps;
            y += (dy * i + (dy < 0 ? -steps / 2 : steps / 2)) / steps;
        }
        for (int ch = 0; ch < 3; ++ch) {
            int c0 = (ca >> (ch * 8)) & 255, c1 = (cb >> (ch * 8)) & 255;
            int c = steps ? (c0 * (steps - i) + c1 * i) / steps : c0;
            color |= (c >> 3) << (ch * 5);
        }
        pixel(x + draw_x, y + draw_y, color, semi);
    }
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
