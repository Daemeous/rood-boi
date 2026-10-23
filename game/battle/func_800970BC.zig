//! Queues a textured quad at the D_800F2280 position into the ordering table
//! named by the scratchpad (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_800970BC (src/BATTLE/BATTLE.PRG/2842C.c)
/// Builds the quad's FT4 primitive at the scratchpad's next-primitive slot and links it into the ordering table.
pub fn func_800970BC() callconv(.c) void {
    const head: *u32 = @ptrFromInt(0x1F800000);
    const link: *u32 = @ptrFromInt(0x1F800004);
    var prim: *align(1) v.POLY_FT4 = @ptrFromInt(@as(usize, head.*));

    // setPolyFT4: length in the tag word's top byte, code in the last byte of the header.
    psx.setBits(@as(*align(1) v.P_TAG, @ptrCast(prim)), "len", 9);
    prim.code = 0x2c;

    // setXY4: the position is re-read for each store, as the C does.
    prim.x0 = psx.conv(i16, @as(c_int, v.g.D_800F2280().vx) -% 11);
    prim.y0 = psx.conv(i16, @as(c_int, v.g.D_800F2280().vy) -% 32);
    prim.x1 = psx.conv(i16, @as(c_int, v.g.D_800F2280().vx) +% 11);
    prim.y1 = psx.conv(i16, @as(c_int, v.g.D_800F2280().vy) -% 32);
    prim.x2 = psx.conv(i16, @as(c_int, v.g.D_800F2280().vx) -% 11);
    prim.y2 = psx.conv(i16, @as(c_int, v.g.D_800F2280().vy));
    prim.x3 = psx.conv(i16, @as(c_int, v.g.D_800F2280().vx) +% 11);
    prim.y3 = psx.conv(i16, @as(c_int, v.g.D_800F2280().vy));

    // setUV4
    prim.u0 = 0xC0;
    prim.v0 = 0;
    prim.u1 = 0xD6;
    prim.v1 = 0;
    prim.u2 = 0xC0;
    prim.v2 = 0x20;
    prim.u3 = 0xD6;
    prim.v3 = 0x20;

    // setRGB0
    prim.r0 = 0xF0;
    prim.g0 = 0x40;
    prim.b0 = 0x20;

    // setTPage(prim, 0, 0, 448, 256): texture page 23.
    prim.tpage = ((0 & 0x3) << 7) | ((0 & 0x3) << 5) | ((256 & 0x100) >> 4) | ((448 & 0x3ff) >> 6) | ((256 & 0x200) << 2);

    // setClut(prim, 960, 221)
    prim.clut = (221 << 6) | ((960 >> 4) & 0x3f);

    // AddPrim(ot - 5, prim++): the link word is the ordering table's entry, 5 u_long (4 bytes each) back.
    const ot: ?*anyopaque = @ptrFromInt(@as(usize, link.* -% 20));
    c.AddPrim(ot, prim);
    prim = psx.at(prim, 1);

    head.* = psx.addr(prim);
}
