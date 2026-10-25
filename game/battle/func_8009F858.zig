//! Reads entry 8 of a battle slot's table, checking its id (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_8009F858 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Returns entry 8's value for slot `arg0`, or -1 when the slot is empty or the entry's id is not 1.
pub fn func_8009F858(arg0: c_int) callconv(.c) c_int {
    // The C indexes D_800F45E0 without a bound check: psx.at does the same.
    const temp_a0: *v.D_800F45E0_t = psx.at(v.g.D_800F45E0(), arg0).* orelse return -1;
    const entry = &temp_a0.unk6C[8];
    if (@as(c_int, psx.getBits(entry, "actorId")) != 1) return -1;
    return @as(c_int, entry.unk1);
}
