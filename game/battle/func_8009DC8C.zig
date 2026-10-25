//! Clears the state flags of a battle actor slot (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_8009DC8C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Clears the state flags of battle actor slot `arg0`, if the slot is filled.
pub fn func_8009DC8C(arg0: c_int) callconv(.c) void {
    // The C indexes D_800F4538 without a bounds check: psx.at keeps that.
    const temp_a3 = psx.at(v.g.D_800F4538(), arg0).*;
    const actor = temp_a3 orelse return;

    actor.unk0.unk11 = 0;
    psx.setBits(&actor.unk0, "unkA_0", 0);
    psx.setBits(actor, "unk5AC_20", 0);
    psx.setBits(&actor.unk0, "unkB_4", 0);
    psx.setBits(&actor.unk0, "unkA_3", 0);
    psx.setBits(&actor.unk0, "unk9_6", 0);
}
