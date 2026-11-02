//! An action effect: the applicable statuses 0x1D; with arg4 the target actor is analyzed (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80083D7C (src/BATTLE/BATTLE.PRG/146C.c)
/// Sets the applicable statuses 0x1D; when arg4 is set, marks the target actor analyzed.
pub fn func_80083D7C(_: ?*v.vs_action_t, _: ?*v._hitEntity_t, target: ?*v._hitEntity_t, _: c_int, arg4: c_int) callconv(.c) c_int {
    _ = c._setApplicableStatuses(0x1D, @ptrCast(target));
    if (arg4 != 0) {
        psx.setBits(psx.get(v.g.vs_battle_actors()[target.?.unk0.targetActor].?.stats).?, "isAnalyzed", 1);
    }
    return 0; // no return statement in the C (the result register's leftover)
}
