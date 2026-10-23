//! Forwards the D_800F1DBC attribute to func_80045BFC, halving the D_80068EB4 entry when arg0 is set (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_800918E8 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Calls func_80045BFC with D_800F1DBC and the D_80068EB4 entry at D_800F1DC4, halved when arg0 is set.
pub fn func_800918E8(arg0: c_int) callconv(.c) void {
    if (v.g.D_800F1DBC().* >= 0) {
        if (arg0 != 0) {
            const value: c_int = @as(c_int, psx.at(v.g.D_80068EB4(), v.g.D_800F1DC4().*).*);
            c.func_80045BFC(0x200, @as(c_int, v.g.D_800F1DBC().*), value >> 2, 8);
        } else {
            const value: c_int = @as(c_int, psx.at(v.g.D_80068EB4(), v.g.D_800F1DC4().*).*);
            c.func_80045BFC(0x200, @as(c_int, v.g.D_800F1DBC().*), value, 8);
        }
    }
}
