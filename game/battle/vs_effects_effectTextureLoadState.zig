//! Returns a battle global word (pc/src/game/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");

/// orig: vs_effects_effectTextureLoadState (pc/src/game/BATTLE/BATTLE.PRG/6F014.c)
/// Returns the word held in global D_800F5680.
pub fn vs_effects_effectTextureLoadState() callconv(.c) c_uint {
    return v.g.D_800F5680().*;
}
