//! Sets the first-person view flag from arg0 (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_setFirstPersonViewDisabled (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_field_clearGeometryOcclusion, then stores the low bit of ~arg0 as the first-person view flag and returns it.
pub fn vs_field_setFirstPersonViewDisabled(arg0: c_int) callconv(.c) c_int {
    c.vs_field_clearGeometryOcclusion();
    const value: c_int = (~arg0) & 1;
    v.g._firstPersonViewEnabled().* = value;
    return value;
}
