//! Stores a value in the first word of vs_effects_textureLoads (pc/src/game/BATTLE/BATTLE.PRG/6F014.c).
const v = @import("views_6F014");

/// orig: vs_effects_setEffectTextureAssetCount (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Stores its argument in the first word of vs_effects_textureLoads.
pub fn vs_effects_setEffectTextureAssetCount(arg0: c_int) callconv(.c) void {
    v.g.vs_effects_textureLoads().assetCount = arg0;
}
