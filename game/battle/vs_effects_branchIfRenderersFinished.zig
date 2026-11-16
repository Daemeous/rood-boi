//! An effect-script opcode: vs_effects_branchScriptIf with whether the script has no effect list (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_branchIfRenderersFinished (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// vs_effects_branchScriptIf(unkD1C.unk34 == NULL, script); returns 1.
pub fn vs_effects_branchIfRenderersFinished(script: ?*v.D_800F53B8_t) callconv(.c) c_int {
    c.vs_effects_branchScriptIf(@intFromBool(psx.get(script.?.unkD1C.unk34) == null), @ptrCast(script));
    return 1;
}
