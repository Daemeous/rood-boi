//! The event script's actor flag 1 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800BAF6C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets (arg0 != 0) or clears bit 0 of func_800A0BE0's flags for every live actor 1..16, then func_8007D15C(arg0).
pub fn func_800BAF6C(arg0: c_short) callconv(.c) void {
    var actor: c_short = 1;
    while (actor < 17) : (actor += 1) { // 1..16
        if (c.func_8007CF64(actor) != null) {
            if (arg0 != 0) {
                c.func_800A087C(actor, @bitCast(c.func_800A0BE0(actor) | 1));
            } else {
                c.func_800A087C(actor, @bitCast(c.func_800A0BE0(actor) & ~@as(c_uint, 1)));
            }
        }
    }
    _ = c.func_8007D15C(arg0);
}
