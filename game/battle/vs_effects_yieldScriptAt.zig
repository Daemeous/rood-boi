//! Stores vs_effects_readScriptHalfword's result in the record's unkA (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_yieldScriptAt (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Stores vs_effects_readScriptHalfword's result in the record's unkA and returns 0.
pub fn vs_effects_yieldScriptAt(arg0: ?*v.vs_effects_EffectInstance) callconv(.c) c_int {
    const result: u16 = c.vs_effects_readScriptHalfword(@ptrCast(arg0));
    arg0.?.cursor = result;
    return 0;
}
