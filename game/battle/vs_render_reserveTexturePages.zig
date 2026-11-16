//! Reserves a run of flag bytes in vs_render_legacyTexturePageReservations (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_render_reserveTexturePages (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Claims `arg1` free flag bytes from index `arg0` (above 0x18, ending below 0x21), fills them with `arg2` and returns `arg0`; otherwise calls nop9 and returns 0.
pub fn vs_render_reserveTexturePages(arg0: c_uint, arg1: c_uint, arg2: c_int) callconv(.c) c_int {
    if (arg0 > 0x18) {
        var var_a3: c_int = 0;
        if ((arg0 +% arg1) < 0x21) {
            const flags = v.g.vs_render_legacyTexturePageReservations();
            var i: c_uint = 0;
            while (i < arg1) : (i +%= 1) {
                if (psx.at(flags, arg0 +% i).* != 0) {
                    var_a3 = 1;
                }
            }
            if (var_a3 == 0) {
                i = 0;
                while (i < arg1) : (i +%= 1) {
                    psx.at(flags, arg0 +% i).* = psx.conv(u8, arg2);
                }
                return psx.conv(c_int, arg0);
            }
        }
    }
    c.vs_main_nop9(100, 0);
    return 0;
}
