//! The gradient gauge of a stat bar (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const sprite_item = common.sprite_item;

/// orig: vs_battle_renderStatBar (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// A tile and a Gouraud quad `w` (0..64) wide at `xy`, blended from colour vs_render_statGaugeColors[index] toward the bar's full colour, linked before `before`.
pub fn renderStatBar(color_index: c_int, w: c_int, before: [*c]c_uint, xy_arg: c_int) callconv(.c) void {
    const xy = xy_arg;
    const rgb1: c_uint = @bitCast(psx.at(v.g.vs_render_statGaugeColors(), color_index).*);
    // Unsigned arithmetic: rgb1 is a u_int, so each term (and its >> 6) is.
    const uw: c_uint = @bitCast(w);
    const rest: c_uint = @bitCast(0x40 -% w);
    const rgb0: c_int = @bitCast((((uw *% 0xFF) +% ((rgb1 & 0xFF) *% rest)) >> 6) |
        ((((uw *% 0xF0) +% (((rgb1 >> 8) & 0xFF) *% rest)) >> 6) << 8) |
        ((((uw *% 0x9E) +% ((rgb1 >> 16) *% rest)) >> 6) << 16));
    // The bar as draw items: a tile (the draw mode the GPU is in), then a Gouraud quad in the dithered mode that leaves mode 0 set;
    // `w` 0 is the tile alone. Each links at `before` (the later first), so the quad goes first and the tile draws before it.
    const xy0: c_uint = @bitCast(xy);
    const xy1: c_uint = xy0 +% ((1 << 16) | 1);
    const xy3: c_uint = xy1 +% ((3 << 16) | 0);
    if (w != 0) {
        const rgb_c = [4]c_int{ rgb0, @bitCast(rgb1), rgb0, @bitCast(rgb1) };
        const xy_c = [4]c_int{ @bitCast(xy1), @bitCast(xy1 +% uw), @bitCast(xy3), @bitCast(xy3 +% uw) };
        sprite_item.g4After(@ptrCast(before), @bitCast(@as(c_uint, 0xE1000200)), @bitCast(@as(c_uint, 0xE1000000)), &rgb_c, &xy_c);
    }
    sprite_item.tileFlat(@ptrCast(before), false, 0x402800, @bitCast(xy0), 0x50042);
}
