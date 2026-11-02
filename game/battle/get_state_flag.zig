//! Reads one byte of the main state flags block (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");

/// orig: vs_battle_getStateFlag (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns byte `arg0` of the main state flags block, the index taken as a signed short as C does.
pub fn getStateFlag(arg0: c_short) callconv(.c) u8 {
    const flags: [*]u8 = @ptrCast(v.g.vs_main_stateFlags());
    return psx.at(flags, arg0).*;
}
