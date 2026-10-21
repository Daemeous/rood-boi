//! Battle unit 38C1C: per-slot update of a D_800F4538 entity (src/BATTLE/BATTLE.PRG/38C1C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_38C1C");
const c = @import("c_38C1C");

/// orig: func_800A2574 (src/BATTLE/BATTLE.PRG/38C1C.c)
/// Runs two follow-up calls for entity slot `arg0`, then sets its unkB_4 field to 6.
pub fn func_800A2574(arg0: c_int, arg1: c_short) callconv(.c) void {
    // The entity pointer is read before the calls; it is unwrapped only at the store, as the C dereferences only there.
    const entities = psx.global([17]?*v.D_800F4538_t, "D_800F4538");
    const temp_s1: ?*v.D_800F4538_t = psx.at(entities, arg0).*;
    c.func_800A9EB4(arg0, @as(c_int, arg1), 8);
    c.func_8009DF3C(arg0, 3);
    psx.setBits(&temp_s1.?.unk0, "unkB_4", 6);
}
