//! An action effect: the applicable statuses 7 on the target, marked when any apply (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_800839E8 (src/BATTLE/BATTLE.PRG/146C.c)
/// Sets the target's applicable statuses from set 7; when that reports any, its unk1C_16 becomes 1.
pub fn func_800839E8(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, _: c_int) callconv(.c) c_int {
    if (c._setApplicableStatuses(7, @ptrCast(target)) != 0) {
        psx.setBits(target.?, "unk1C_16", 1);
    }
    return 0; // no return statement in the C (the result register's leftover)
}
