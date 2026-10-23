//! Floating number digits: bouncing textured quads (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80094B0C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Draws the digits of a floating number at x, y as bouncing textured quads.
pub fn func_80094B0C(x_in: c_int, y_in: c_int, p: ?*v.D_800F1BAC_t) callconv(.c) void {
    const q = p.?;
    const colors = [_]v.CVECTOR{
        .{ .r = 128, .@"g" = 128, .b = 128, .cd = 0 },
        .{ .r = 0, .@"g" = 128, .b = 0, .cd = 0 },
        .{ .r = 128, .@"g" = 0, .b = 128, .cd = 0 },
        .{ .r = 0, .@"g" = 128, .b = 128, .cd = 0 },
    };
    const bounce = [_]i8{ 0, -6, -12, -14, -16, -10, -5, -1, 0, -3, -6, -7, -8, -5, -3, -1, 0, -2, -1, 0 };
    var x = x_in;
    var y = y_in;
    var frame: c_int = 15;
    var color: c_int = 0;
    var offset: c_int = 0;
    var i: c_int = 0;

    if (q.unkE >= 3) {
        if ((q.unkC & 0x8000) == 0) {
            y = y -% (@as(c_int, q.unkB) >> 1);
        } else {
            y = y +% (@as(c_int, q.unkB) >> 1);
        }
        i = c.func_80094E18(x, y, psx.cptr(c.D_800F1BAC_t, q));
        color = 0;
        offset = 0;
    } else {
        if (q.unkB < 16) {
            frame = @as(c_int, q.unkB);
        }
        color = (@as(c_int, q.unkE) & 1) << 1;
        i = @as(c_int, q.displayTextLen) *% 3;
        if ((q.unkC & 0x8000) == 0) {
            offset = bounce[@intCast(frame)];
            color +%= 1;
        } else {
            offset = bounce[@intCast(frame +% 4)];
        }
    }

    x = x +% (i -% 6);

    const ci: usize = @intCast(color);
    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    var cursor: usize = psx.addr(psx.get(scratch.unk0).?);
    var prim: usize = cursor;

    i = @as(c_int, q.displayTextLen) -% 1;
    while (i >= 0) : ({
        i -%= 1;
        x -%= 6;
    }) {
        const digitY: c_int = y +% offset;
        offset = @divTrunc(offset, 2);
        if (psx.conv(c_uint, x +% 8) < 328) {
            const pp: *align(1) v.POLY_FT4 = @ptrFromInt(prim);
            const tag: *align(1) v.P_TAG = @ptrFromInt(prim);

            // setPolyFT4: len 9, code 0x2c
            psx.setBits(tag, "len", 9);
            tag.code = 0x2c;

            // setXY4
            pp.x0 = psx.conv(i16, x);
            pp.y0 = psx.conv(i16, digitY -% 6);
            pp.x1 = psx.conv(i16, x +% 8);
            pp.y1 = psx.conv(i16, digitY -% 6);
            pp.x2 = psx.conv(i16, x);
            pp.y2 = psx.conv(i16, digitY +% 6);
            pp.x3 = psx.conv(i16, x +% 8);
            pp.y3 = psx.conv(i16, digitY +% 6);

            // setShadeTex(prim, 0): clears bit 0 of code
            tag.code &= 0xFE;

            // setRGB0
            pp.r0 = colors[ci].r;
            pp.g0 = colors[ci].@"g";
            pp.b0 = colors[ci].b;

            // setUV4: glyph of the character at displayText[i]
            const glyph: c_int = @as(c_int, psx.byteAt(i8, q, @as(isize, i) + 20).*) -% '0';
            const base: c_int = glyph *% 8;
            pp.u0 = psx.conv(u8, base +% 64);
            pp.v0 = 0;
            pp.u1 = psx.conv(u8, base +% 72);
            pp.v1 = 0;
            pp.u2 = psx.conv(u8, base +% 64);
            pp.v2 = 12;
            pp.u3 = psx.conv(u8, base +% 72);
            pp.v3 = 12;

            // setTPage(prim, 0, 0, 768, 0)
            pp.tpage = (768 & 0x3ff) >> 6;
            // setClut(prim, 832, 223)
            pp.clut = (223 << 6) | ((832 >> 4) & 0x3f);

            cursor +%= @sizeOf(v.POLY_FT4);
            const ot_addr: u32 = psx.addr(psx.get(scratch.unk4).?) -% 20;
            c.AddPrim(
                @as(?*anyopaque, @ptrFromInt(@as(usize, ot_addr))),
                @as(?*anyopaque, @ptrFromInt(prim)),
            );
            prim +%= @sizeOf(v.POLY_FT4);
        }
    }

    psx.put(&scratch.unk0, @as(?*anyopaque, @ptrFromInt(cursor)));
}
