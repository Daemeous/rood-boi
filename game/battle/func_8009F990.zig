//! Dispatches the 30DB0 unit's per-slot routine over one slot, or over all 17 when arg0 is -1 (src/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009F990 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Runs func_8009F9F4 on slot arg0, or on slots 0..16 when arg0 is -1.
pub fn func_8009F990(arg0: c_int, arg1: ?*v.D_800F4538_unk64) callconv(.c) void {
    if (arg0 == -1) {
        var i: c_int = 0;
        while (i < 17) : (i +%= 1) {
            c.func_8009F9F4(i, @ptrCast(arg1));
        }
    } else {
        c.func_8009F9F4(arg0, @ptrCast(arg1));
    }
}
