//! Battle unit 5BF94: sets a unit's state byte from a helper's result (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_loadTimelineImmediate (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Passes the low byte of vs_effects_readScriptByte's result to vs_effects_loadEffectTimeline, sets unkA to 0xB7, and returns 1.
pub fn vs_effects_loadTimelineImmediate(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const flags: c_int = c.vs_effects_readScriptByte(@ptrCast(arg0)) & 0xFF;
    c.vs_effects_loadEffectTimeline(@ptrCast(arg0), flags, 0);
    arg0.?.cursor = 0xB7;
    return 1;
}
