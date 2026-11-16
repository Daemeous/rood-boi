//! Battle unit 5BF94.c: stores a computed value in the entry's unkA field (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_jumpScript (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Stores vs_effects_readScriptHalfword's result in the entry's unkA and returns 1.
pub fn vs_effects_jumpScript(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const value = c.vs_effects_readScriptHalfword(@ptrCast(arg0));
    arg0.?.unkA = value;
    return 1;
}
