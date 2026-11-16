//! Checks a battle entry's table slot against its second value and reports the result (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_branchIfScriptRegisterEqual (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Compares the slot `unk10` selects against the second value and passes the result on; always returns 1.
pub fn vs_effects_branchIfScriptRegisterEqual(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const self = arg0.?;
    const handle: ?*c.D_800F53B8_t = @ptrCast(self);

    // Object order: the index call, then the slot load, then the second call, then the compare.
    const index = c.vs_effects_readScriptByte(handle);
    const entry: c_int = @as(c_int, psx.at(&self.unk10, index).*);
    const other: c_int = @as(c_int, c.vs_effects_readScriptHalfword(handle));

    c.vs_effects_branchScriptIf(@intFromBool(entry == other), handle);
    return 1;
}
