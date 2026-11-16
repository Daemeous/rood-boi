//! Calls vs_effects_branchScriptIf with the flag from D_800F522C, then returns 1 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D48E4 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Passes `arg0` to vs_effects_branchScriptIf with a flag that is set when D_800F522C is zero; always returns 1.
pub fn func_800D48E4(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    c.vs_effects_branchScriptIf(@intFromBool(v.g.D_800F522C().* == 0), @ptrCast(arg0));
    return 1;
}
