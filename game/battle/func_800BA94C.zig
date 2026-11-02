//! Battle wait check on a script byte: holds while the state is 2 and the byte is 0 or 5 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA94C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Holds a battle wait on byte 1 of the script while the state is 2, releasing it when func_8007BEE4 matches that byte.
pub fn func_800BA94C(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    if (v.g.D_800F4C2C().* == 2 and (arg0[1] == 0 or arg0[1] == 5)) {
        if (v.g.D_800F4BB9().* != 0) {
            if (c.func_8007BEE4() == @as(c_int, arg0[1])) {
                v.g.D_800F4BB9().* = 0;
                return 0;
            }
        } else {
            c.func_800CF1A8();
            v.g.D_800F4BB9().* = 1;
        }
        return 1;
    }
    return @as(c_int, @intFromBool(c.func_8007BEE4() != @as(c_int, arg0[1])));
}
