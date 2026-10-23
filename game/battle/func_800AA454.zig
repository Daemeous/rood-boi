//! Forwards a call to func_800AA490 with the unk5C0 field of the enemy's entry in D_800F4538
//! (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800AA454 (src/BATTLE/BATTLE.PRG/40564.c)
/// Calls func_800AA490 with the unk5C0 value of D_800F4538[arg0].
pub fn func_800AA454(arg0: c_int, arg1: ?*v.func_8006EBF8_t_fields, arg2: c_int) callconv(.c) void {
    // D_800F4538[arg0] is dereferenced unconditionally in the C: `.?` matches that.
    const unk5C0: c_int = @as(c_int, psx.at(v.g.D_800F4538(), arg0).*.?.unk5C0);
    c.func_800AA490(arg0, @ptrCast(arg1), arg2, unk5C0);
}
