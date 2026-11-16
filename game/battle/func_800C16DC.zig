//! Clears the byte at offset 0x2C of the battle block at vs_battle_targetSphere, if it is set
//! (pc/src/game/BATTLE/BATTLE.PRG/58578.c).
const v = @import("views_58578");

/// orig: func_800C16DC (pc/src/game/BATTLE/BATTLE.PRG/58578.c)
/// Clears the flag byte at 0x2C of the block in vs_battle_targetSphere when that block is set.
pub fn func_800C16DC() callconv(.c) void {
    if (v.g.vs_battle_targetSphere().*) |block| {
        block.unk2C = 0;
    }
}
