//! Pass-through to the battle routine at 0x800AAA88 (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AAA68 (src/BATTLE/BATTLE.PRG/40564.c)
/// Forwards its three arguments unchanged to func_800AAA88.
pub fn func_800AAA68(arg0: c_int, arg1: ?*v.SVECTOR, arg2: c_int) callconv(.c) void {
    c.func_800AAA88(arg0, @ptrCast(arg1), arg2);
}
