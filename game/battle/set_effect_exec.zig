//! Stores a battle effect renderer in its slot of the renderer table
//! (src/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// orig: vs_battle_setEffectExec (src/BATTLE/BATTLE.PRG/6F014.c)
/// Sets the renderer of effect slot `index` to `func`.
pub fn setEffectExec(func: c.effectExec, index: c_int) callconv(.c) void {
    psx.at(v.g.vs_battle_effectRenderers(), index).* = @ptrCast(func);
}
