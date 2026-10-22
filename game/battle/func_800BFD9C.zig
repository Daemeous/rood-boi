//! Runs the per-slot update for each flagged slot, then the shared tail (src/BATTLE/BATTLE.PRG/573B8.c).
const v = @import("views_573B8");
const c = @import("c_573B8");

/// orig: func_800BFD9C (src/BATTLE/BATTLE.PRG/573B8.c)
/// Calls func_8007D260 for each of the 17 slots flagged in D_800F4B70, then func_800BBDDC.
pub fn func_800BFD9C() callconv(.c) void {
    const flags = v.g.D_800F4B70();
    var i: c_int = 0;
    while (i < 17) : (i +%= 1) {
        // i is 0..16 here, so the index is in range.
        if (flags[@intCast(i)] != 0) {
            c.func_8007D260(i);
        }
    }
    c.func_800BBDDC();
}
