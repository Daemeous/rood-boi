//! Returns the battle word vs_effects_primaryPhase as an int (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: func_800CF48C (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Returns the 32-bit word vs_effects_primaryPhase, converted to int as C does.
pub fn func_800CF48C() callconv(.c) c_int {
    return psx.conv(c_int, v.g.vs_effects_primaryPhase().*);
}
