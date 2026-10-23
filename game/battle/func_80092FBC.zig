//! Advances or checks a timed or conditional step: runs one of two checks by mode and counts a frame down (src/BATTLE/BATTLE.PRG/2842C.c).
const c = @import("c_2842C");
const v = @import("views_2842C");

/// orig: func_80092FBC (src/BATTLE/BATTLE.PRG/2842C.c)
/// Runs the mode's check on `arg0`, then returns 0 when its end condition is met, else 1.
pub fn func_80092FBC(arg0: ?*v.func_80092F74_t, arg1: ?*v.func_80092F74_t2) callconv(.c) c_int {
    var var_a0: c_int = 0;
    const new_var = arg1.?;

    if (new_var.unk4[0].u16[1] == 1) {
        var_a0 = c.func_8009306C(@ptrCast(arg0));
    } else if (new_var.unk4[0].u16[1] == 2) {
        var_a0 = c.func_80093364(@ptrCast(arg0));
    }

    if (@as(c_int, new_var.unk4[0].u16[0]) == 0x8000) {
        if (var_a0 == 0) {
            return 0;
        }
    } else {
        const self = arg0.?;
        // ++arg0->unk18: wraps as the C's 32-bit int does.
        self.unk18 +%= 1;
        if (self.unk18 >= @as(c_int, new_var.unk4[0].u16[0])) {
            self.unk18 = 0;
            return 0;
        }
    }
    return 1;
}
