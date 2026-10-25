//! Clears a pending flag in D_800F244F, after releasing it through func_800AB2AC
//! (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009D208 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Releases entry arg0 of D_800F244F and clears it; returns -3 if the release call fails.
pub fn func_8009D208(arg0: c_int) callconv(.c) c_int {
    if (psx.at(v.g.D_800F244F(), arg0).* != 0) {
        c.func_800AB2AC(arg0 -% 1);
        // The C reads the entry again after the call, so the argument is re-read here.
        if (c.func_8007E0A8(psx.conv(c_uint, psx.at(v.g.D_800F244F(), arg0).*), 1, 4) == 0) {
            return -3;
        }
        psx.at(v.g.D_800F244F(), arg0).* = 0;
    }
    return 0;
}
