//! An action effect: marks the source entity's unk1C_26 2 (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: func_800842F0 (src/BATTLE/BATTLE.PRG/146C.c)
/// Sets the source entity's unk1C_26 to 2.
pub fn func_800842F0(_: ?*v.vs_action_t, source: ?*v._hitEntity_t, _: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    psx.setBits(source.?, "unk1C_26", 2);
    return 0; // no return statement in the C (the result register's leftover)
}
