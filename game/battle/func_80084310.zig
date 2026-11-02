//! An action effect: with arg4, state flag 0x40 set (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80084310 (src/BATTLE/BATTLE.PRG/146C.c)
/// When arg4 is set, sets state flag 0x40.
pub fn func_80084310(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, _: ?*v._hitEntity_t, _: c_int, arg4: c_int) callconv(.c) c_int {
    if (arg4 != 0) {
        c.vs_battle_setStateFlag(0x40, 1);
    }
    return 0; // no return statement in the C (the result register's leftover)
}
