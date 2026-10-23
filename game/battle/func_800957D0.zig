//! Draws one shaded textured quad (an FT4 primitive) at the scratchpad's
//! packet cursor and links it into the ordering table (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_800957D0 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Builds a 16x10 textured quad centred on (arg0 + 8, arg1), takes its texture column from arg2, and adds it to the ordering table.
pub fn func_800957D0(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) void {
    // The packet cursor lives in the scratchpad at 0x1F800000; the ordering table slot at 0x1F800004.
    const prim_addr: usize = @as(*const u32, @ptrFromInt(0x1F800000)).*;
    const prim: *align(1) v.POLY_FT4 = @ptrFromInt(prim_addr);
    const tag: *align(1) v.P_TAG = @ptrCast(prim);

    psx.setBits(tag, "len", 9);
    tag.code = 0x2C;

    const x: c_int = arg0 -% 8;
    // arg2 <<= 4 wraps as a 32-bit shift.
    const tex: c_int = @bitCast(@as(c_uint, @bitCast(arg2)) << 4);

    prim.x0 = psx.conv(i16, x);
    prim.y0 = psx.conv(i16, arg1 -% 5);
    prim.x1 = psx.conv(i16, x +% 16);
    prim.y1 = psx.conv(i16, arg1 -% 5);
    prim.x2 = psx.conv(i16, x);
    prim.y2 = psx.conv(i16, arg1 +% 5);
    prim.x3 = psx.conv(i16, x +% 16);
    prim.y3 = psx.conv(i16, arg1 +% 5);

    prim.u0 = psx.conv(u8, tex +% 0x40);
    prim.v0 = 0x30;
    prim.u1 = psx.conv(u8, tex +% 0x50);
    prim.v1 = 0x30;
    prim.u2 = psx.conv(u8, tex +% 0x40);
    prim.v2 = 0x3A;
    prim.u3 = psx.conv(u8, tex +% 0x50);
    prim.v3 = 0x3A;

    prim.tpage = 0x17;
    prim.clut = 0x373E;
    // setShadeTex(prim, 1): semi-transparency/shade bit on.
    tag.code |= 0x01;

    // The ordering table pointer is the scratchpad slot at +4, minus 5 u_long entries (20 bytes).
    const ot_guest: usize = @as(*const u32, @ptrFromInt(0x1F800004)).*;
    const ot: ?*anyopaque = @ptrFromInt(ot_guest -% 20);
    c.AddPrim(ot, prim);

    // prim++ then the cursor is stored back into the scratchpad.
    const next: *align(1) v.POLY_FT4 = @ptrFromInt(prim_addr +% @sizeOf(v.POLY_FT4));
    @as(*u32, @ptrFromInt(0x1F800000)).* = psx.addr(next);
}
