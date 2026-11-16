//! Calls vs_effects_branchScriptIf with whether the unit's unk8 matches vs_effects_readScriptByte's result (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_branchIfParticleCountEqual (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Passes whether `arg0.unk8` equals vs_effects_readScriptByte(arg0) on to vs_effects_branchScriptIf; always returns 1.
pub fn vs_effects_branchIfParticleCountEqual(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    // The load of unk8 comes before the call, as GCC's gimplifier orders the operands of `==`.
    const unk8: u8 = arg0.?.unk8;
    const result: u8 = c.vs_effects_readScriptByte(@ptrCast(arg0));
    const flag: c_int = @intFromBool(unk8 == result);
    c.vs_effects_branchScriptIf(flag, @ptrCast(arg0));
    return 1;
}
