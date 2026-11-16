//! Battle helper in 5BF94.c: runs vs_effects_readScriptHalfword on the block and passes its result on to vs_effects_tickEffectTimeline.
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_tickTimelineFromScript (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Calls vs_effects_tickEffectTimeline on the block with the value vs_effects_readScriptHalfword returns for it; always 1.
pub fn vs_effects_tickTimelineFromScript(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const r = c.vs_effects_readScriptHalfword(@ptrCast(arg0));
    c.vs_effects_tickEffectTimeline(@ptrCast(arg0), psx.conv(c_int, r));
    return 1;
}
