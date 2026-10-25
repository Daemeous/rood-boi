//! Battle unit 30DB0: copies a 0xE0-byte block into D_800F4448 (src/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_80099960 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies 0xE0 bytes from `src` into the block at D_800F4448.
pub fn func_80099960(src: ?*anyopaque) callconv(.c) void {
    c.vs_main_memcpy(@ptrCast(v.g.D_800F4448()), src, 0xE0);
}
