//! Runs the pending entries (indices 1..15) of the table D_800F2260 through func_80095C18 while D_800F2270 is clear (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_8009627C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Calls func_80095C18 for each nonzero entry 1..15 of D_800F2260, unless D_800F2270 is set.
pub fn func_8009627C() callconv(.c) void {
    if (v.g.D_800F2270().* == 0) {
        var i: c_int = 1;
        while (i < 16) : (i +%= 1) {
            const table = v.g.D_800F2260();
            const entry: c_int = table[@intCast(i)];
            if (entry != 0) {
                c.func_80095C18(i, entry);
            }
        }
    }
}
