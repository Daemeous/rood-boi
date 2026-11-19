/* Initial CPU polygon rasterizer for native-port bring-up. Affine UVs (taken
 * at pixel corners, as the PS1 does), packed RGB555/CLUT textures, Gouraud
 * colors, top-left edge ownership, and PS1 blend modes. Interpolation
 * precision and dithering are not yet hardware-exact. */
typedef struct {
    int x, y, u, v, color[3];
    int z; /* GTE view depth (SZ) for projected vertices, 0 for 2D */
    float fx, fy; /* unrounded screen position (PGXP-style), with draw offset */
} pc_gpu_vertex;
static int64_t edge(const pc_gpu_vertex* a, const pc_gpu_vertex* b, int x2, int y2)
{
    return (int64_t)(b->x - a->x) * (y2 - a->y * 2)
         - (int64_t)(b->y - a->y) * (x2 - a->x * 2);
}
static int floor_div(int64_t n, int64_t d) /* d > 0 */
{
    return (int)(n >= 0 ? n / d : -((-n + d - 1) / d));
}
static int top_left(const pc_gpu_vertex* a, const pc_gpu_vertex* b)
{
    return b->y < a->y || (b->y == a->y && b->x > a->x);
}
static void triangle(
    pc_gpu_vertex a, pc_gpu_vertex b, pc_gpu_vertex c, unsigned code, unsigned clut)
{
    int64_t area = edge(&a, &b, c.x * 2, c.y * 2);
    if (!area)
        return;
    if (area < 0) {
        pc_gpu_vertex swap = b;
        b = c;
        c = swap;
        area = -area;
    }
    int minx = a.x, maxx = a.x, miny = a.y, maxy = a.y;
    pc_gpu_vertex vertex[2] = { b, c };
    for (int i = 0; i < 2; ++i) {
        if (vertex[i].x < minx)
            minx = vertex[i].x;
        if (vertex[i].x > maxx)
            maxx = vertex[i].x;
        if (vertex[i].y < miny)
            miny = vertex[i].y;
        if (vertex[i].y > maxy)
            maxy = vertex[i].y;
    }
    /* PS1 rejects primitives whose coordinate span exceeds the GPU limits. */
    if (maxx - minx >= 1024 || maxy - miny >= 512)
        return;
    {
        int xy[6] = { a.x, a.y, b.x, b.y, c.x, c.y }, uv[6] = { a.u, a.v, b.u, b.v, c.u, c.v };
        int rgb[9] = { a.color[0], a.color[1], a.color[2], b.color[0], b.color[1], b.color[2],
            c.color[0], c.color[1], c.color[2] };
        int clip[4] = { clip_x, clip_y, clip_w, clip_h };
        int depth[3] = { a.z, b.z, c.z };
        float precise[6] = { a.fx, a.fy, b.fx, b.fy, c.fx, c.fy };
        pc_hwc_set_depth(depth);
        pc_hwc_set_precise(precise);
        /* Covered by a native draw without reference packets: only the
         * hardware target gets it (gpu_hw.c keeps the area stale). */
        pc_hwc_raster_post();
        if (pc_hwc_triangle(xy, uv, rgb, code, tpage, clut, texture_window, clip, mask_set, mask_check))
            return;
    }
    if (minx < clip_x)
        minx = clip_x;
    if (miny < clip_y)
        miny = clip_y;
    if (maxx >= clip_x + clip_w)
        maxx = clip_x + clip_w - 1;
    if (maxy >= clip_y + clip_h)
        maxy = clip_y + clip_h - 1;
    if (minx < 0)
        minx = 0;
    if (miny < 0)
        miny = 0;
    if (maxx > 1023)
        maxx = 1023;
    if (maxy > 511)
        maxy = 511;
    if (maxx < minx || maxy < miny)
        return;
    /* Where the target's picture replaces native VRAM before any read (a
     * drawing area native draws left stale), nothing is drawn here. */
    if (pc_hw_raster_skip && pc_hw_raster_skip(minx, miny, maxx, maxy)) {
        pc_cost_count(PC_COUNT_SKIPPED_PX, (unsigned)((maxx - minx + 1) * (maxy - miny + 1)));
        return;
    }
    if (pc_raster_off) /* (the null renderer: no pixels, the items and the stream are what it produces) */
        return;
    pc_hwc_mark_raster(minx, miny, maxx, maxy);
    uint64_t cost = pc_cost_begin();
    pc_cost_count(PC_COUNT_RASTER_PX, (unsigned)((maxx - minx + 1) * (maxy - miny + 1)));
    raster_pixels += (unsigned long long)((maxx - minx + 1) * (maxy - miny + 1));
    int tl0 = top_left(&b, &c), tl1 = top_left(&c, &a), tl2 = top_left(&a, &b);
    for (int y = miny; y <= maxy; ++y)
        for (int x = minx; x <= maxx; ++x) {
            int64_t w0 = edge(&b, &c, x * 2 + 1, y * 2 + 1);
            int64_t w1 = edge(&c, &a, x * 2 + 1, y * 2 + 1);
            int64_t w2 = edge(&a, &b, x * 2 + 1, y * 2 + 1);
            if (w0 < 0 || (!w0 && !tl0) || w1 < 0 || (!w1 && !tl1) || w2 < 0
                || (!w2 && !tl2))
                continue;
            unsigned color = 0;
            if (code & 4) {
                /* The PS1 interpolates texture coordinates at the pixel's
                 * top-left corner, rounding to nearest (Mednafen's +0.5 on
                 * the base value): a flipped sprite (U falling to the right)
                 * samples its own texels, not one past them as the centre would. */
                int64_t c0 = edge(&b, &c, x * 2, y * 2), c1 = edge(&c, &a, x * 2, y * 2), c2 = edge(&a, &b, x * 2, y * 2);
                int u = floor_div(c0 * a.u + c1 * b.u + c2 * c.u + area / 2, area);
                int v = floor_div(c0 * a.v + c1 * b.v + c2 * c.v + area / 2, area);
                color = texture(u, v, clut);
                if (!color)
                    continue;
            }
            if (!(code & 4) || !(code & 1)) {
                unsigned shaded = color & 0x8000;
                for (int ch = 0; ch < 3; ++ch) {
                    int shade =
                        (w0 * a.color[ch] + w1 * b.color[ch] + w2 * c.color[ch]) / area;
                    int component =
                        code & 4 ? ((color >> (ch * 5)) & 31) * shade / 128 : shade / 8;
                    shaded |= clamp5(component) << (ch * 5);
                }
                color = shaded;
            }
            pixel(x, y, color, (code & 2) && (!(code & 4) || (color & 0x8000)));
        }
    pc_cost_end(PC_COST_RASTER, cost);
}
static unsigned polygon_command(const uint32_t* p, unsigned available)
{
    unsigned code = p[0] >> 24;
    int count = code & 8 ? 4 : 3, textured = !!(code & 4), gouraud = !!(code & 16);
    unsigned words = 1 + count * (1 + textured) + (gouraud ? count - 1 : 0);
    if (available < words)
        pc_unimplemented("truncated polygon packet");
    pc_gpu_vertex vertex[4];
    unsigned cursor = 1, rgb = p[0], clut = 0;
    int screen[4][2];
    for (int i = 0; i < count; ++i) {
        if (i && gouraud)
            rgb = p[cursor++];
        unsigned xy = p[cursor++];
        vertex[i].z = 0;
        vertex[i].x = (int16_t)xy + draw_x;
        vertex[i].y = (int16_t)(xy >> 16) + draw_y;
        vertex[i].fx = (float)vertex[i].x;
        vertex[i].fy = (float)vertex[i].y;
        screen[i][0] = (int16_t)xy, screen[i][1] = (int16_t)(xy >> 16);
        for (int ch = 0; ch < 3; ++ch)
            vertex[i].color[ch] = (rgb >> (ch * 8)) & 255;
        vertex[i].u = vertex[i].v = 0;
        if (textured) {
            unsigned uv = p[cursor++];
            vertex[i].u = uv & 255;
            vertex[i].v = (uv >> 8) & 255;
            if (!i)
                clut = uv >> 16;
            if (i == 1)
                tpage = (tpage & ~0x1ff) | ((uv >> 16) & 0x1ff);
        }
    }
    if (textured && pc_button_roles_texels) {
        /* The game's ○ / × follow the button roles (button_roles.c). */
        int u0 = vertex[0].u, v0 = vertex[0].v, u1 = u0, v1 = v0;
        for (int i = 1; i < count; ++i) {
            u0 = vertex[i].u < u0 ? vertex[i].u : u0, u1 = vertex[i].u > u1 ? vertex[i].u : u1;
            v0 = vertex[i].v < v0 ? vertex[i].v : v0, v1 = vertex[i].v > v1 ? vertex[i].v : v1;
        }
        /* (a quad's far edge: the texel before it) */
        int du = pc_button_roles_texels(vram, tpage, clut, u0, v0, u1 > u0 ? u1 - 1 : u1, v1 > v0 ? v1 - 1 : v1);
        for (int i = 0; du && i < count; ++i)
            vertex[i].u += du;
    }
    unsigned packet_xy[4];
    for (int i = 0; i < count; ++i)
        packet_xy[i] = (unsigned)(uint16_t)screen[i][0] | ((unsigned)(uint16_t)screen[i][1] << 16);
    /* 2D text glyphs placed between whole pixels (ui_upscale.c, ui_subpixel):
     * the hardware path draws them where the game put them before rounding. */
    float corners[8];
    if (textured && pc_ui_precise_lookup && pc_ui_precise_lookup(precise_slot_for(p), packet_xy, count, corners))
        for (int i = 0; i < count; ++i) {
            vertex[i].fx = corners[i * 2] + draw_x;
            vertex[i].fy = corners[i * 2 + 1] + draw_y;
        }
    if (textured && pc_hw_sync_texture) {
        int u0 = 255, v0 = 255, u1 = 0, v1 = 0;
        for (int i = 0; i < count; ++i) {
            u0 = vertex[i].u < u0 ? vertex[i].u : u0, u1 = vertex[i].u > u1 ? vertex[i].u : u1;
            v0 = vertex[i].v < v0 ? vertex[i].v : v0, v1 = vertex[i].v > v1 ? vertex[i].v : v1;
        }
        int clip[4] = { clip_x, clip_y, clip_w, clip_h };
        pc_hwc_raster_post();
        pc_hwc_fence_texture(tpage, clut, texture_window, u0, v0, u1, v1, clip);
    }
    /* ui_text: a battle number popup's digit at the UI text scale (ui_text.c; scale 1: unchanged). */
    if (textured && count == 4 && drawing_tag && drawing_tag + 1 == p && pc_ui_text_popup_place) {
        float placed[8]; /* (as the packet's: without the drawing offset) */
        for (int i = 0; i < 4; ++i)
            placed[i * 2] = vertex[i].fx - draw_x, placed[i * 2 + 1] = vertex[i].fy - draw_y;
        int item = pc_frame_command_item ? pc_frame_command_item(GUEST(p)) : -1; /* (a draw item's quad: its group is the popup) */
        if (item >= 0 && pc_ui_text_popup_place_item ? pc_ui_text_popup_place_item(item, placed)
                                                     : pc_ui_text_popup_place(drawing_tag, next_packet, placed))
            for (int i = 0; i < 4; ++i) {
                placed[i * 2] += draw_x, placed[i * 2 + 1] += draw_y;
                vertex[i].fx = placed[i * 2], vertex[i].fy = placed[i * 2 + 1];
                vertex[i].x = round_px(placed[i * 2]), vertex[i].y = round_px(placed[i * 2 + 1]);
            }
    }
    {   /* (the digest: where the vertices end up, after ui_text's popup placement and precise corners) */
        int v[16], n = 0;
        for (int i = 0; i < count; ++i) {
            float fx = vertex[i].fx, fy = vertex[i].fy;
            v[n++] = vertex[i].x, v[n++] = vertex[i].y;
            memcpy(&v[n++], &fx, sizeof fx), memcpy(&v[n++], &fy, sizeof fy);
        }
        stream_record(0x103, v, n);
    }
    pc_hwc_set_2d(1); /* (everything that reaches here is 2D: what 3D draws is native) */
    if (narrowed_2d((const int (*)[2])screen, count))
        pc_hwc_set_narrowed();
    if (textured) {
        int xy[6] = { vertex[0].x, vertex[0].y, vertex[1].x, vertex[1].y, vertex[2].x, vertex[2].y };
        int uv[6] = { vertex[0].u, vertex[0].v, vertex[1].u, vertex[1].v, vertex[2].u, vertex[2].v };
        pc_hwc_set_flipped_uv(xy, uv);
    }
    {
        /* Widescreen: 2D standing for the whole screen draws over the full view. */
        int x0 = vertex[0].x, y0 = vertex[0].y, x1 = x0, y1 = y0, clip[4] = { clip_x, clip_y, clip_w, clip_h };
        int texels[4] = { vertex[0].u, vertex[0].v, vertex[0].u, vertex[0].v };
        for (int i = 1; i < count; ++i) {
            x0 = vertex[i].x < x0 ? vertex[i].x : x0, x1 = vertex[i].x > x1 ? vertex[i].x : x1;
            y0 = vertex[i].y < y0 ? vertex[i].y : y0, y1 = vertex[i].y > y1 ? vertex[i].y : y1;
            texels[0] = vertex[i].u < texels[0] ? vertex[i].u : texels[0];
            texels[1] = vertex[i].v < texels[1] ? vertex[i].v : texels[1];
            texels[2] = vertex[i].u > texels[2] ? vertex[i].u : texels[2];
            texels[3] = vertex[i].v > texels[3] ? vertex[i].v : texels[3];
        }
        pc_hwc_set_bounds(x0, y0, x1, y1, code, tpage, clut, textured ? texels : NULL, clip);
    }
    pc_hwc_set_packet(GUEST(p));
    /* ui_text_layout line: a scaled glyph cell (a zooming talk bubble's
     * text) is laid out with its line (gpu_hw.c); its triangles below are
     * then the original, drawn while the layout is off. */
    int text_piece = 0;
    if (textured && count == 4) {
        float fxy[8];
        int fuv[8];
        if (!(pc_ui_precise_find && pc_ui_precise_find(precise_slot_for(p), packet_xy, count, fxy)))
            for (int i = 0; i < 4; ++i)
                fxy[i * 2] = (float)vertex[i].x - draw_x, fxy[i * 2 + 1] = (float)vertex[i].y - draw_y;
        for (int i = 0; i < 4; ++i) {
            fxy[i * 2] += draw_x, fxy[i * 2 + 1] += draw_y;
            fuv[i * 2] = vertex[i].u, fuv[i * 2 + 1] = vertex[i].v;
        }
        int clip[4] = { clip_x, clip_y, clip_w, clip_h };
        pc_gpu_command = GUEST(p);
        pc_hwc_raster_post(); /* (what the CPU drew so far is in the renderer's VRAM before it samples for the quad) */
        text_piece = pc_hwc_text_quad(fxy, fuv, vertex[0].color, code, tpage, clut, texture_window, clip, mask_set,
            mask_check);
        pc_gpu_command = NULL;
    }
    triangle(vertex[0], vertex[1], vertex[2], code, clut);
    if (count == 4)
        triangle(vertex[1], vertex[2], vertex[3], code, clut);
    if (text_piece) {
        pc_hwc_raster_post(); /* (the quad's own rasterized rectangles reach the renderer before it draws the piece) */
        pc_hwc_text_piece_done();
    }
    return words;
}
