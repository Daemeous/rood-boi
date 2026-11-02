//! Dispatches script action codes 0 and 1 to the 4A0A8 helpers (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BA194 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs the helper for action code arg0[1] on the entry in arg0[2]; the second argument is unused.
pub fn func_800BA194(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const temp_s0: c_int = arg0[2];

    switch (@as(c_int, arg0[1])) {
        0 => {
            if (v.g.D_800F4C2C().* == 2 and c.func_80093764(temp_s0) != 0) {
                c.func_80093788(temp_s0);
            }
            _ = c.func_8009291C(temp_s0);
        },
        1 => {
            _ = c.func_800936F8(temp_s0);
        },
        else => {},
    }
    return 0;
}
