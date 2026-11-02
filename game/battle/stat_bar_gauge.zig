//! The gradient gauge of a stat bar (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_battle_renderStatBar (src/BATTLE/BATTLE.PRG/5BF94.c)
/// A tile and a Gouraud quad `w` (0..64) wide at `xy`, blended from colour D_800EBC04[index] toward the bar's full colour, linked before `before`.
pub fn renderStatBar(color_index: c_int, w: c_int, before: [*c]c_uint, xy_arg: c_int) callconv(.c) void {
    var xy = xy_arg;
    var rgb1: c_uint = @bitCast(psx.at(v.g.D_800EBC04(), color_index).*);
    const prim: [*]u32 = @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk0).?));
    // Unsigned arithmetic: rgb1 is a u_int, so each term (and its >> 6) is.
    const uw: c_uint = @bitCast(w);
    const rest: c_uint = @bitCast(0x40 -% w);
    const rgb0: c_int = @bitCast((((uw *% 0xFF) +% ((rgb1 & 0xFF) *% rest)) >> 6) |
        ((((uw *% 0xF0) +% (((rgb1 >> 8) & 0xFF) *% rest)) >> 6) << 8) |
        ((((uw *% 0x9E) +% ((rgb1 >> 16) *% rest)) >> 6) << 16));
    prim[0] = (before.* & 0xFFFFFF) | @as(c_uint, if (w == 0) 0x03000000 else 0x0D000000);
    prim[1] = (96 << 24) | (0x40 << 16) | (0x28 << 8) | 0x00; // primTile, rgb 0x00, 0x28, 0x40
    prim[2] = @bitCast(xy);
    prim[3] = 0x50042;
    prim[4] = (0xE1 << 24) | (1 << 9); // tpage: dithering on, 4-bit CLUT, half transparency, page 0
    prim[5] = @bitCast(rgb0 | (56 << 24)); // primPolyG4
    xy +%= (1 << 16) | 1;
    prim[6] = @bitCast(xy);
    prim[7] = rgb1;
    prim[8] = @bitCast(xy +% w);
    xy +%= (3 << 16) | 0;
    prim[9] = @bitCast(rgb0);
    prim[10] = @bitCast(xy);
    prim[11] = rgb1;
    rgb1 = @bitCast(w);
    prim[12] = @as(c_uint, @bitCast(xy)) +% rgb1;
    prim[13] = 0xE1 << 24; // tpage 0
    before.* = (psx.addr(prim) << 8) >> 8;
    psx.put(&v.g.vs_scratch().unk0, @as(*anyopaque, @ptrCast(prim + 14)));
}
