//! Checks and sets the flag bytes for slots 25..32 in vs_render_legacyTexturePageReservations (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_render_retireTexturePages (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets slots arg0..arg0+arg1-1 to -3 when all hold arg2 and returns 1; otherwise reports and returns 0.
pub fn vs_render_retireTexturePages(arg0: c_uint, arg1: c_uint, arg2: c_int) callconv(.c) c_int {
    var var_s0: c_int = 0;

    if (25 <= arg0 and arg0 +% arg1 < 33) {
        const slots = v.g.vs_render_legacyTexturePageReservations();
        var i: c_uint = 0;
        while (i < arg1) : (i +%= 1) {
            if (@as(c_int, psx.at(slots, arg0 +% i).*) != arg2) {
                var_s0 = 1;
            }
        }

        if (var_s0 != 0) {
            c.vs_main_nop9(101, 0);
        } else {
            if (psx.conv(c_uint, var_s0) < arg1) {
                i = 0;
                while (i < arg1) : (i +%= 1) {
                    psx.at(slots, arg0 +% i).* = psx.conv(u8, @as(c_int, -3));
                }
            }
        }

        return @intFromBool(var_s0 == 0);
    }

    c.vs_main_nop9(101, 0);
    return 0;
}
