//! Packs a 12-bit field from a 16-bit header into the top bits of a word
//! (src/BATTLE/BATTLE.PRG/7CD68.c).
const psx = @import("common").psx;
const v = @import("views_7CD68");

/// orig: func_800E5A74 (src/BATTLE/BATTLE.PRG/7CD68.c)
/// Stores into `dst` the bits 0x FFE of `src.unk4` shifted to bits 20..31, keeping the low 21 bits of `dst`.
pub fn func_800E5A74(dst: [*c]c_int, src: ?*v.func_800E5A74_t) callconv(.c) void {
    // The shift stays in c_uint: the 0xFFE field's top bit lands on bit 31, as the C's wrapping int does.
    const field: c_uint = @as(c_uint, src.?.unk4 & 0xFFE) << 20;
    dst[0] = @bitCast(field | (@as(c_uint, @bitCast(dst[0])) & 0x1FFFFF));
}
