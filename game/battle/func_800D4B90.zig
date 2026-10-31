//! Calls func_800D46DC with whether the unit's unk8 matches func_800D5170's result (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D4B90 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Passes whether `arg0.unk8` equals func_800D5170(arg0) on to func_800D46DC; always returns 1.
pub fn func_800D4B90(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    // The load of unk8 comes before the call, as GCC's gimplifier orders the operands of `==`.
    const unk8: u8 = arg0.?.unk8;
    const result: u8 = c.func_800D5170(@ptrCast(arg0));
    const flag: c_int = @intFromBool(unk8 == result);
    c.func_800D46DC(flag, @ptrCast(arg0));
    return 1;
}
