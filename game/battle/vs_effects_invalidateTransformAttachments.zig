//! Clears a field of one of a script's entries (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_effects_invalidateTransformAttachments (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Bits 0 and 1 of the high half of unk1C[index].unk4 (the PS1's script + 0x22 + index * 0xD0), unchecked as in the C.
pub fn vs_effects_invalidateTransformAttachments(script: ?*v.D_800F53B8_t, index: u8) callconv(.c) void {
    psx.at(&script.?.unk1C, index).unk4 &= ~@as(i32, 3 << 16);
}
