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
/* Whether the last projection lookup used the scene camera's projection;
 * other GTE drawing (the minimap) is screen furniture: no depth, so it is
 * neither lit nor blurred. */
static int scene_projection(void)
{
    float h, ox, oy;
    if (!pc_scene_game_projection(&h, &ox, &oy))
        return 1;
    return (unsigned short)pc_gte_lookup_view[2] == (unsigned short)h
        && pc_gte_lookup_view[0] / 65536.0f - ox < 0.5f && ox - pc_gte_lookup_view[0] / 65536.0f < 0.5f
        && pc_gte_lookup_view[1] / 65536.0f - oy < 0.5f && oy - pc_gte_lookup_view[1] / 65536.0f < 0.5f;
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
        pc_hw_set_depth(depth);
        pc_hw_set_precise(precise);
        /* Covered by a native draw without reference packets: only the
         * hardware target gets it (gpu_hw.c keeps the area stale). */
        if (pc_hw_triangle(xy, uv, rgb, code, tpage, clut, texture_window, clip, mask_set, mask_check))
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
    pc_hw_mark(minx, miny, maxx, maxy);
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
    int projected = 1; /* every vertex came from the GTE this frame (3D) */
    int absent = 0, missing = -1;
    unsigned spread = 0; /* sequence spread of the vertices taken (diagnostics) */
    int candidates[4], slots[4][4], screen[4][2];
    unsigned sequences[4][4];
    for (int i = 0; i < count; ++i) {
        if (i && gouraud)
            rgb = p[cursor++];
        unsigned xy = p[cursor++];
        vertex[i].z = 0;
        vertex[i].x = (int16_t)xy + draw_x;
        vertex[i].y = (int16_t)(xy >> 16) + draw_y;
        vertex[i].fx = (float)vertex[i].x;
        vertex[i].fy = (float)vertex[i].y;
        candidates[i] = pc_gte_log ? pc_gte_projected_candidates((int16_t)xy, (int16_t)(xy >> 16), slots[i], sequences[i], 4) : 0;
        screen[i][0] = (int16_t)xy, screen[i][1] = (int16_t)(xy >> 16);
        if (!candidates[i])
            ++absent, missing = i;
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
    /* A room polygon is always 3D: should one vertex's log entry be gone
     * anyway, it borrows another vertex's candidates for the choice below
     * and then takes the others' mean depth, rather than the whole polygon
     * dropping to 2D (unlit, black under the lighting) for a frame. */
    int room = pc_hw_is_room_packet(p), room_sz[4];
    unsigned packet_xy[4];
    for (int i = 0; i < count; ++i)
        packet_xy[i] = (unsigned)(uint16_t)screen[i][0] | ((unsigned)(uint16_t)screen[i][1] << 16);
    int exact = room && pc_room_exact_depth && pc_gte_log && pc_hw_room_depth && pc_hw_room_depth(p, packet_xy, count, room_sz);
    if (exact) {
        /* Room geometry: the depths room_geometry.c recorded with the
         * packet; the log only lends the unrounded position of a projection
         * with the same depth. */
        for (int i = 0; i < count; ++i) {
            vertex[i].z = room_sz[i];
            /* The closest depth's position (any is within the pixel; never
             * whole pixels one frame and unrounded the next: that shook). */
            int nearest = candidates[i] ? 1 << 30 : 0;
            for (int k = 0; k < candidates[i]; ++k) {
                int z;
                float fx, fy;
                pc_gte_projection_entry(slots[i][k], screen[i][0], screen[i][1], &z, &fx, &fy);
                int off = z > room_sz[i] ? z - room_sz[i] : room_sz[i] - z;
                if (off < nearest) {
                    nearest = off;
                    vertex[i].fx = fx + draw_x;
                    vertex[i].fy = fy + draw_y;
                }
            }
            if (nearest > 64 && getenv("ROOD_DEPTH_DEBUG")) {
                static int mismatches;
                if (mismatches++ < 200)
                    fprintf(stderr, "[depth] room poly %p frame %u vertex %d: recorded %d, nearest projection off by %d\n",
                        (void*)p, frames, i, room_sz[i], nearest);
            }
        }
        absent = 0, missing = -1;
    } else if (absent == 1 && room) {
        int other = missing ? 0 : 1;
        candidates[missing] = candidates[other];
        memcpy(slots[missing], slots[other], sizeof slots[missing]);
        memcpy(sequences[missing], sequences[other], sizeof sequences[missing]);
    } else if (absent) {
        projected = 0;
        missing = -1;
    }
    if (projected && !exact) {
        /* Several projections can share a pixel: take the vertices projected
         * together (closest sequence numbers), not whichever came last. */
        int pick[4] = { 0, 0, 0, 0 }, best[4] = { 0, 0, 0, 0 };
        unsigned best_spread = ~0u;
        for (;;) {
            unsigned lo = sequences[0][pick[0]], hi = lo;
            for (int i = 1; i < count; ++i) {
                unsigned q = sequences[i][pick[i]];
                lo = q - lo > 0x80000000u ? q : lo;
                hi = hi - q > 0x80000000u ? q : hi;
            }
            if (hi - lo < best_spread)
                best_spread = hi - lo, memcpy(best, pick, sizeof best);
            int i = 0;
            while (i < count && ++pick[i] == candidates[i])
                pick[i++] = 0;
            if (i == count)
                break;
        }
        spread = best_spread;
        pc_hw_caller = pc_gte_projection_caller(slots[0][best[0]]);
        for (int i = 0; i < count; ++i) {
            pc_gte_projection_entry(slots[i][best[i]], screen[i][0], screen[i][1], &vertex[i].z, &vertex[i].fx,
                &vertex[i].fy);
            vertex[i].fx += draw_x;
            vertex[i].fy += draw_y;
            /* Room packets are the scene whatever the projection (it can
             * differ from the camera's for a frame while the view zooms). */
            if (!room && !scene_projection())
                vertex[i].z = 0;
        }
        if (missing >= 0) {
            int sum = 0;
            for (int i = 0; i < count; ++i)
                if (i != missing)
                    sum += vertex[i].z;
            vertex[missing].z = sum / (count - 1);
            vertex[missing].fx = (float)vertex[missing].x;
            vertex[missing].fy = (float)vertex[missing].y;
        }
    }
    if (getenv("ROOD_DEPTH_DEBUG") && room) {
        /* Diagnostics: room polygons that lose their depth. */
        static int reported;
        int scene = projected ? scene_projection() : -1;
        if ((!projected || !scene || missing >= 0 || spread > 64) && reported < 400) {
            ++reported;
            float h = 0, ox = 0, oy = 0;
            pc_scene_game_projection(&h, &ox, &oy);
            fprintf(stderr, "[depth] room poly %p frame %u, vertex %d borrowed: candidates", (void*)p, frames, missing);
            for (int i = 0; i < count; ++i)
                fprintf(stderr, " %d(%d,%d)", candidates[i], screen[i][0], screen[i][1]);
            fprintf(stderr, " spread %u z %d %d %d %d", spread, vertex[0].z, vertex[1].z, vertex[2].z, count == 4 ? vertex[3].z : 0);
            fprintf(stderr, " scene %d view %d,%d,%d camera %.1f,%.1f,%.0f\n", scene, pc_gte_lookup_view[0] >> 16,
                pc_gte_lookup_view[1] >> 16, pc_gte_lookup_view[2], ox, oy, h);
        }
    }
    if (!projected || exact)
        pc_hw_caller = NULL;
    /* 2D text glyphs placed between whole pixels (ui_upscale.c, ui_subpixel):
     * the hardware path draws them where the game put them before rounding. */
    float corners[8];
    if (!projected && textured && pc_ui_precise_lookup && pc_ui_precise_lookup(p, packet_xy, count, corners))
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
        pc_hw_sync_texture(tpage, clut, texture_window, u0, v0, u1, v1);
    }
    pc_hw_set_2d(!projected);
    if (!projected && textured) {
        int xy[6] = { vertex[0].x, vertex[0].y, vertex[1].x, vertex[1].y, vertex[2].x, vertex[2].y };
        int uv[6] = { vertex[0].u, vertex[0].v, vertex[1].u, vertex[1].v, vertex[2].u, vertex[2].v };
        pc_hw_set_flipped_uv(xy, uv);
    }
    if (!projected) {
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
        pc_hw_set_bounds(x0, y0, x1, y1, code, tpage, textured ? texels : NULL, clip);
    }
    pc_hw_set_room(room);
    pc_hw_set_packet(p);
    triangle(vertex[0], vertex[1], vertex[2], code, clut);
    if (count == 4)
        triangle(vertex[1], vertex[2], vertex[3], code, clut);
    return words;
}
