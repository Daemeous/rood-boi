//! Passes a sub-block of an entry in the D_800F4538 table on to func_800AE4FC
//! (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: func_800AE68C (src/BATTLE/BATTLE.PRG/44F14.c)
/// Calls func_800AE4FC on the first block of table entry `arg0`, with `arg1 + 4`.
pub fn func_800AE68C(arg0: c_int, arg1: c_int) callconv(.c) void {
    // D_800F4538[arg0]: the C does not check arg0 against the table's 17 entries.
    const entry = psx.at(v.g.D_800F4538(), arg0).*.?;
    c.func_800AE4FC(@ptrCast(&entry.unk0), arg1 +% 4);
}
