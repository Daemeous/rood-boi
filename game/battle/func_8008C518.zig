//! Battle helper wrapper: calls vs_field_setMovingPlatformActor with -1 as its second argument (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const c = @import("c_146C");

/// orig: func_8008C518 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_field_setMovingPlatformActor with `arg0` and -1, discarding its result.
pub fn func_8008C518(arg0: c_int) callconv(.c) void {
    _ = c.vs_field_setMovingPlatformActor(arg0, -1);
}
