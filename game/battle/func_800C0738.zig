//! Clears the first byte of the battle record at vs_battle_targetSphere when it is set (pc/src/game/BATTLE/BATTLE.PRG/573B8.c).
const v = @import("views_573B8");

/// orig: func_800C0738 (pc/src/game/BATTLE/BATTLE.PRG/573B8.c)
/// Clears the flag byte at offset 0x10 of the battle record, if the record exists.
pub fn func_800C0738() callconv(.c) void {
    if (v.g.vs_battle_targetSphere().*) |record| {
        record.unk0[1].unk0 = 0;
    }
}
