//! Sets the unk22_2 flag of an entry (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: func_800D6508 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// entries[index].unk22_2 = 1.
pub fn func_800D6508(entries: ?*v.func_800D6508_t, index: u8) callconv(.c) void {
    psx.setBits(psx.at(entries.?, index), "unk22_2", 1);
}
