//! Clears the unk22_0 field of an entry (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: func_800D65A8 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// entries[index].unk22_0 = 0.
pub fn func_800D65A8(entries: ?*v.func_800D6508_t, index: u8) callconv(.c) void {
    psx.setBits(psx.at(entries.?, index), "unk22_0", 0);
}
