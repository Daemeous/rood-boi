//! An effect-script opcode: vs_actors_setWeaponBrightnessOverride on a kind-4 target (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_sourceWeaponBrightness (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// For a kind-4 unkC target: vs_actors_setWeaponBrightnessOverride(its id's low byte, 1, value), or (.., 0, 0) when value is 0.
pub fn vs_effects_sourceWeaponBrightness(script: ?*v.vs_effects_EffectInstance, value: u8) callconv(.c) void {
    const s = script.?;
    if (s.context.unkC.unk0 == 4) {
        if (value != 0) {
            c.vs_actors_setWeaponBrightnessOverride(@as(c_int, psx.conv(u8, s.context.unkC.unk4.unk0)), 1, value);
        } else {
            c.vs_actors_setWeaponBrightnessOverride(@as(c_int, psx.conv(u8, s.context.unkC.unk4.unk0)), 0, 0);
        }
    }
}
