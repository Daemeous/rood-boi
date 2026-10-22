//! Clears the byte at offset 0x2C of the battle block at D_800EB9B8, if it is set
//! (src/BATTLE/BATTLE.PRG/58578.c).
const v = @import("views_58578");

/// orig: func_800C16DC (src/BATTLE/BATTLE.PRG/58578.c)
/// Clears the flag byte at 0x2C of the block in D_800EB9B8 when that block is set.
pub fn func_800C16DC() callconv(.c) void {
    if (v.g.D_800EB9B8().*) |block| {
        block.unk2C = 0;
    }
}
