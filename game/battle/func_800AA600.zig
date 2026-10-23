//! Forwards a call to func_800AA698 in the same unit (src/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AA600 (src/BATTLE/BATTLE.PRG/40564.c)
/// Forwards its three arguments unchanged to func_800AA698.
pub fn func_800AA600(arg0: c_int, arg1: ?*v.SVECTOR, arg2: c_int) callconv(.c) void {
    const vec: [*c]c.SVECTOR = if (arg1) |p| psx.cptr(c.SVECTOR, p) else null;
    c.func_800AA698(arg0, vec, arg2);
}
