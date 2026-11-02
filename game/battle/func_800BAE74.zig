//! Misc-count decrement helper of the battle overlay (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: func_800BAE74 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Reads the short at `arg0 + 1` and decrements the battle misc count by it; returns 0.
pub fn func_800BAE74(arg0: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = arg1;
    // arg0 + 1 as C computes it: a wrapping address add, no null check.
    const p: [*c]u8 = @ptrFromInt(@intFromPtr(arg0) +% 1);
    _ = c.vs_battle_decreaseMiscCount(@as(c_int, c.vs_battle_getShort(p)));
    return 0;
}
