//! Passes a slot's value to func_800AA490 (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800A9CDC (src/BATTLE/BATTLE.PRG/40564.c)
/// Calls func_800AA490 with arg2, or with the unk5C0 of the slot arg0 when arg2 is -1.
pub fn func_800A9CDC(arg0: c_int, arg1: ?*v.func_8006EBF8_t_fields, arg2: c_int) callconv(.c) void {
    if (arg2 == -1) {
        // D_800F4538[arg0] is indexed unchecked, as the C does; a null entry traps where the C faults.
        const new_var = psx.at(v.g.D_800F4538(), arg0).*.?;
        c.func_800AA490(arg0, @ptrCast(arg1), -1, @as(c_int, new_var.unk5C0));
    } else {
        c.func_800AA490(arg0, @ptrCast(arg1), -1, arg2);
    }
}
