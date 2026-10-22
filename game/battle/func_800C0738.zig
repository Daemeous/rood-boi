//! Clears the first byte of the battle record at D_800EB9B8 when it is set (src/BATTLE/BATTLE.PRG/573B8.c).
const v = @import("views_573B8");

/// orig: func_800C0738 (src/BATTLE/BATTLE.PRG/573B8.c)
/// Clears the flag byte at offset 0x10 of the battle record, if the record exists.
pub fn func_800C0738() callconv(.c) void {
    if (v.g.D_800EB9B8().*) |record| {
        record.unk10.unk0 = 0;
    }
}
