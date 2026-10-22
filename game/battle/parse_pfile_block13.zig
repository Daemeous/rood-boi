//! Parse-file block 13 hookup for the battle overlay (src/BATTLE/BATTLE.PRG/6F014.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6F014");
const c = @import("c_6F014");

/// orig: _parsePfileBlock13 (src/BATTLE/BATTLE.PRG/6F014.c)
/// Stores block 13's guest address in the parse-file state and records the value func_80046608 derives from it.
pub fn parsePfileBlock13(arg0: ?*anyopaque) callconv(.c) void {
    const state = v.g.D_800F569C().*.?;
    psx.put(&state.block13Data, arg0);
    state.unkCC = c.func_80046608(psx.addr(arg0));
}
