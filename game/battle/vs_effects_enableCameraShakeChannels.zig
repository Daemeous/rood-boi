//! Sets flag bits in the battle global vs_effects_enabledShakeChannels (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");

/// orig: vs_effects_enableCameraShakeChannels (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Sets the bits of `arg0` in the flag byte vs_effects_enabledShakeChannels.
pub fn vs_effects_enableCameraShakeChannels(arg0: c_int) callconv(.c) void {
    const flags = v.g.vs_effects_enabledShakeChannels();
    flags.* = psx.conv(u8, @as(c_int, flags.*) | arg0);
}
