//! Builds a text box's frame quads (a body and a tail) into the ordering table and returns the box width (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80094E18 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Lays out the two frame primitives of a text box ending at x, y and returns its width.
pub fn func_80094E18(x0: c_int, y: c_int, p: ?*v.D_800F1BAC_t) callconv(.c) c_int {
    const q = p.?;
    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    const idx: c_int = @as(c_int, q.unkE) -% 3;
    const rect = psx.at(v.g.D_80068FAC(), psx.at(v.g.D_800E861C(), idx).*);
    var prim: *v.POLY_FT4 = @ptrCast(@alignCast(psx.get(scratch.unk0).?));
    var sign: c_int = q.unkC;
    const width: c_int = @divTrunc(@as(c_int, q.displayTextLen) *% 6 +% @as(c_int, rect.w) +% 12, 2);
    var x = x0 -% width;

    psx.setBits(@as(*v.P_TAG, @ptrCast(prim)), "len", 9);
    prim.code = 0x2c;
    prim.x0 = psx.conv(i16, x);
    prim.y0 = psx.conv(i16, y -% 6);
    prim.x1 = psx.conv(i16, x +% @as(c_int, rect.w));
    prim.y1 = psx.conv(i16, y -% 6);
    prim.x2 = psx.conv(i16, x);
    prim.y2 = psx.conv(i16, y +% 6);
    prim.x3 = psx.conv(i16, x +% @as(c_int, rect.w));
    prim.y3 = psx.conv(i16, y +% 6);
    prim.code |= 1;
    prim.u0 = psx.conv(u8, rect.x);
    prim.v0 = psx.conv(u8, rect.y);
    prim.u1 = psx.conv(u8, @as(c_int, rect.x) +% @as(c_int, rect.w));
    prim.v1 = psx.conv(u8, rect.y);
    prim.u2 = psx.conv(u8, rect.x);
    prim.v2 = psx.conv(u8, @as(c_int, rect.y) +% 12);
    prim.u3 = psx.conv(u8, @as(c_int, rect.x) +% @as(c_int, rect.w));
    prim.v3 = psx.conv(u8, @as(c_int, rect.y) +% 12);
    prim.tpage = ((0 & 0x3) << 7) | ((0 & 0x3) << 5) | ((256 & 0x100) >> 4) | ((448 & 0x3ff) >> 6) | ((256 & 0x200) << 2);
    prim.clut = (220 << 6) | ((976 >> 4) & 0x3f);
    sign = if ((sign & 0x8000) != 0) 0 else 8;
    c.AddPrim(@ptrFromInt(@intFromPtr(psx.get(scratch.unk4).?) -% 20), @ptrCast(prim));
    prim = @ptrCast(@as([*]v.POLY_FT4, @ptrCast(prim)) + 1);

    x = x +% (@as(c_int, rect.w) +% 4);

    psx.setBits(@as(*v.P_TAG, @ptrCast(prim)), "len", 9);
    prim.code = 0x2c;
    prim.x0 = psx.conv(i16, x);
    prim.y0 = psx.conv(i16, y -% 5);
    prim.x1 = psx.conv(i16, x +% 8);
    prim.y1 = psx.conv(i16, y -% 5);
    prim.x2 = psx.conv(i16, x);
    prim.y2 = psx.conv(i16, y +% 5);
    prim.x3 = psx.conv(i16, x +% 8);
    prim.y3 = psx.conv(i16, y +% 5);
    prim.u0 = psx.conv(u8, sign +% 154);
    prim.v0 = 76;
    prim.u1 = psx.conv(u8, sign +% 162);
    prim.v1 = 76;
    prim.u2 = psx.conv(u8, sign +% 154);
    prim.v2 = 86;
    prim.u3 = psx.conv(u8, sign +% 162);
    prim.v3 = 86;
    prim.tpage = ((0 & 0x3) << 7) | ((0 & 0x3) << 5) | ((0 & 0x100) >> 4) | ((768 & 0x3ff) >> 6) | ((0 & 0x200) << 2);
    prim.clut = (223 << 6) | ((864 >> 4) & 0x3f);
    prim.code |= 1;
    c.AddPrim(@ptrFromInt(@intFromPtr(psx.get(scratch.unk4).?) -% 20), @ptrCast(prim));
    prim = @ptrCast(@as([*]v.POLY_FT4, @ptrCast(prim)) + 1);

    psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(prim)));
    return width;
}
