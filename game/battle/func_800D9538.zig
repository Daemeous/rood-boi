//! Copies the scratchpad word at 0x1F8003F8 into D_800F58C4 (src/BATTLE/BATTLE.PRG/6FC00.c).
const v = @import("views_6FC00");

/// orig: func_800D9538 (src/BATTLE/BATTLE.PRG/6FC00.c)
/// Copies the scratchpad word at 0x1F8003F8 into D_800F58C4.
pub fn func_800D9538() callconv(.c) void {
    const scratch: *const i32 = @ptrFromInt(0x1F8003F8);
    v.g.D_800F58C4().* = scratch.*;
}
