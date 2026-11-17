//! An effect-script opcode: vs_battle_applyActionSourceEffects on a target (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D63D0 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// vs_battle_applyActionSourceEffects(id low byte, NULL) for a kind-4 unk0 target, else vs_battle_applyActionSourceEffects(its unk6, its data).
pub fn func_800D63D0(script: ?*v.vs_effects_EffectInstance) callconv(.c) void {
    const s = script.?;
    if (s.context.unk0.unk0 == 4) {
        c.vs_battle_applyActionSourceEffects(@as(c_int, psx.conv(u8, s.context.unk0.unk4.unk0)), null);
    } else {
        c.vs_battle_applyActionSourceEffects(s.context.unk0.unk4.unk6, @ptrCast(&s.context.unk0.unk4));
    }
}
