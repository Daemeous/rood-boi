//! Stores vs_effects_readScriptByte's result for a battle record in D_800F5610
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D47C8 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Runs vs_effects_readScriptByte on the record, stores its result in D_800F5610 and returns 1.
pub fn func_800D47C8(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    v.g.D_800F5610().* = psx.conv(i32, c.vs_effects_readScriptByte(@ptrCast(arg0)));
    return 1;
}
