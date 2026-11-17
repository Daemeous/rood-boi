//! Sets a flag of one of a script's entries (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_effects_resumeTransformMotion (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Bit 2 of the high half of unk1C[index].unk4 (the PS1's script + 0x22 + index * 0xD0), unchecked as in the C.
pub fn vs_effects_resumeTransformMotion(script: ?*v.vs_effects_EffectInstance, index: u8) callconv(.c) void {
    psx.at(&script.?.unk1C, index).unk4 |= 1 << 18;
}
