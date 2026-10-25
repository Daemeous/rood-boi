//! Clears the per-slot fields of a battle actor slot (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_800A0ABC (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Clears the timer, the counter and one flag of battle slot `arg0`, if the slot is occupied.
pub fn func_800A0ABC(arg0: c_int) callconv(.c) void {
    // No bounds check: the C indexes D_800F4538 with arg0 unchecked.
    const temp_a0 = psx.at(v.g.D_800F4538(), arg0).*;
    if (temp_a0) |a| {
        a.unk1840 = 0;
        a.unk1846 = 0;
        psx.setBits(a, "unk5AC_15", 0);
    }
}
