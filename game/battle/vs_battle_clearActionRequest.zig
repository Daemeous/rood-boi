//! Clears a 0x84C-byte battle block (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: vs_battle_clearActionRequest (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Zeroes the 0x84C-byte block at `block`.
pub fn vs_battle_clearActionRequest(block: ?*anyopaque) callconv(.c) void {
    c.vs_main_bzero(block, 0x84C);
}
