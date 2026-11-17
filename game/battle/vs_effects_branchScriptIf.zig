//! Advances an entry's `unkA` field, or sets it from `vs_effects_readScriptHalfword` (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_branchScriptIf (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Sets `unkA` of the entry from `vs_effects_readScriptHalfword` when `arg0` is nonzero, else adds 2 to it.
pub fn vs_effects_branchScriptIf(arg0: c_int, arg1: ?*v.D_800F53B8_t) callconv(.c) void {
    if (arg0 != 0) {
        const value = c.vs_effects_readScriptHalfword(@ptrCast(arg1));
        arg1.?.cursor = value;
    } else {
        arg1.?.cursor +%= 2;
    }
}
