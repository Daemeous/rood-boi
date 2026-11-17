//! An effect-script opcode: vs_battle_applyActionTargetEffects on a target (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D6388 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// vs_battle_applyActionTargetEffects(id low byte, NULL) for a kind-4 unk18 target, else vs_battle_applyActionTargetEffects(its unk6, its data).
pub fn func_800D6388(script: ?*v.vs_effects_EffectInstance) callconv(.c) void {
    const s = script.?;
    if (s.context.unk18.unk0 == 4) {
        c.vs_battle_applyActionTargetEffects(@as(c_int, psx.conv(u8, s.context.unk18.unk4.unk0)), null);
    } else {
        c.vs_battle_applyActionTargetEffects(s.context.unk18.unk4.unk6, @ptrCast(&s.context.unk18.unk4));
    }
}
