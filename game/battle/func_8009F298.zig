//! Battle helper for the 30DB0 unit (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009F298 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Runs the unit's setup for `slot` and, when `flag` is 1, calls func_800A01C8 for slot 0xFE if its entry reports a result.
pub fn func_8009F298(slot: c_int, vec: ?*v.SVECTOR, flag: c_int) callconv(.c) void {
    const vec_c: [*c]c.SVECTOR = if (vec) |p| psx.cptr(c.SVECTOR, p) else null;
    c.func_8009EFEC(slot, vec_c, flag);
    if (flag == 1) {
        var sp10: [*c]c.u_short = null;
        _ = c.func_800AD494(@ptrCast(psx.at(v.g.D_800F4538(), slot).*), 0xFE, &sp10);
        if (sp10 != null) {
            _ = c.func_800A01C8(slot, 0xFE, 8, 0);
        }
    }
}
