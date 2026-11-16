//! Random roll below a bound: a curve sample or a stored value (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_randomCameraShakePhaseStep (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Rolls a random value below the bound: the curve sampled when the flag is set, else the stored value; 0 when the bound is 0.
pub fn vs_effects_randomCameraShakePhaseStep(arg0: ?*v.D_800F54B8_t) callconv(.c) u8 {
    const p = arg0.?;
    const var_s0: c_int = if (psx.getBits(p, "unk1_6") != 0)
        c.vs_battle_sampleCurve(@as(c_int, p.unk3), @as(c_int, p.unk0))
    else
        @as(c_int, p.unk3);

    if (var_s0 == 0) {
        return 0;
    }

    // rand() is non-negative, so the remainder is non-negative whatever the sign of var_s0.
    return psx.conv(u8, @rem(c.rand(), var_s0));
}
