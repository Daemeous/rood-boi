//! Copies a battle actor's words at 0x1C and 0x5C into their shadow fields and
//! its 0x26 halfword into 0x52 (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");

/// orig: vs_actors_saveActorGroundedTransform (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Copies the words at 0x5C and 0x1C into 0x60 and 0x4C, the word at 0x5C into 0x1868 unless bit C_0 is set, and 0x26 into 0x52.
pub fn vs_actors_saveActorGroundedTransform(arg0: ?*v.D_800F45E0_t) callconv(.c) void {
    const a = arg0.?;
    a.unk60 = psx.fieldAt(i32, &a.unk5C, 0).*;
    psx.fieldAt(v.func_800ACF54_t, &a.unk28, 0x24).* = psx.fieldAt(v.func_800ACF54_t, &a.unk1C, 0).*;
    if (psx.getBits(a, "unkC_0") == 0) {
        a.unk1868 = psx.fieldAt(i32, &a.unk5C, 0).*;
    }
    a.unk52 = a.unk26;
}
