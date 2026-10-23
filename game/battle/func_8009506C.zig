//! Appends the four textured quads of a bar, sized by a step from `data.unkB`, to the
//! ordering table (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_8009506C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Builds the four quads of a bar at (left, centerY) into the packet cursor and links them into the ordering table.
pub fn func_8009506C(left: c_int, centerY: c_int, arg2: ?*v.D_800F1BAC_t) callconv(.c) void {
    const data = arg2.?;
    const unkB: c_int = data.unkB;
    const step: c_int = if (unkB < 4) unkB else if (unkB >= 27) 30 -% unkB else 4;

    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    const heights = v.g.D_800E8634();
    const rects = v.g.D_800E8640();

    var x: c_int = left -% 16;
    const half: c_int = @as(c_int, psx.at(heights, step).*);
    const y0: c_int = centerY -% half;
    const y1: c_int = centerY +% half;

    var prim: *align(1) v.POLY_FT4 = @ptrCast(psx.get(scratch.unk0).?);

    var i: usize = 0;
    while (i < 4) : (i += 1) {
        const rect = rects[i];
        const w: c_int = rect.w;
        if ((x < 320) and ((x +% w) >= 0)) {
            const tag: *align(1) v.P_TAG = @ptrCast(prim);
            psx.setBits(tag, "len", 9);
            tag.code = 0x2c;
            prim.x0 = psx.conv(i16, x);
            prim.y0 = psx.conv(i16, y0);
            prim.x1 = psx.conv(i16, x +% w);
            prim.y1 = psx.conv(i16, y0);
            prim.x2 = psx.conv(i16, x);
            prim.y2 = psx.conv(i16, y1);
            prim.x3 = psx.conv(i16, x +% w);
            prim.y3 = psx.conv(i16, y1);
            tag.code |= 0x01;

            const rx: c_int = rect.x;
            const ry: c_int = rect.y;
            prim.u0 = psx.conv(u8, rx);
            prim.v0 = psx.conv(u8, ry);
            prim.u1 = psx.conv(u8, rx +% w);
            prim.v1 = psx.conv(u8, ry);
            prim.u2 = psx.conv(u8, rx);
            prim.v2 = psx.conv(u8, ry +% 10);
            prim.u3 = psx.conv(u8, rx +% w);
            prim.v3 = psx.conv(u8, ry +% 10);
            tag.code |= 0x02;

            // setTPage(prim, 0, 0, 768, 0): (768 & 0x3ff) >> 6 is 12, the rest is 0.
            prim.tpage = (768 & 0x3ff) >> 6;
            // setClut(prim, 864, 223)
            prim.clut = (223 << 6) | ((864 >> 4) & 0x3f);

            c.AddPrim(
                @ptrFromInt(@intFromPtr(psx.get(scratch.unk4).?) -% 20),
                @ptrCast(prim),
            );
            prim = @ptrFromInt(@intFromPtr(prim) +% @sizeOf(v.POLY_FT4));
        }

        x +%= w;
    }

    psx.put(&scratch.unk0, prim);
}
