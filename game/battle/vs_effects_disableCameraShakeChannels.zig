//! Clears bits in the battle flag byte vs_effects_enabledShakeChannels, or the whole byte when the argument is zero
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_effects_disableCameraShakeChannels (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Clears the bits of `mask` from the flag byte, or clears the whole byte when `mask` is zero.
pub fn vs_effects_disableCameraShakeChannels(arg0: c_int) callconv(.c) void {
    const flags = v.g.vs_effects_enabledShakeChannels();
    if (arg0 != 0) {
        // The C's `&= ~arg0` is computed in int, then stored as u8.
        flags.* = psx.conv(u8, @as(c_int, flags.*) & ~arg0);
        return;
    }
    flags.* = 0;
}
