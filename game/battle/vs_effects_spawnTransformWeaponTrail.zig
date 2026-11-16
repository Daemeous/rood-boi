//! An effect-script opcode: func_800CF7A8 between vs_effects_saveEffectResourceContext and vs_effects_restoreEffectResourceContext (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_spawnTransformWeaponTrail (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// func_800CF7A8(entry's unk34, unk35, value % 10, value / 10) for entry unk1C[index] (unchecked, as in the C).
pub fn vs_effects_spawnTransformWeaponTrail(script: ?*v.D_800F53B8_t, index: u8, value: u8) callconv(.c) void {
    const s = script.?;
    c.vs_effects_saveEffectResourceContext();
    const entry = psx.at(&s.unk1C, index);
    c.func_800CF7A8(entry.unk34, entry.unk35, value % 10, value / 10);
    c.vs_effects_restoreEffectResourceContext();
}
