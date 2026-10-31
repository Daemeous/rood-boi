//! Copies a 16-byte block into the global D_800F5620 (src/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: func_800D5260 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Copies the block at `src` over the global D_800F5620, all 16 bytes.
pub fn func_800D5260(src: ?*v.D_800F5620_t) callconv(.c) void {
    v.g.D_800F5620().* = src.?.*;
}
