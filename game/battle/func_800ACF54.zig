//! Copies a battle actor's words at 0x1C and 0x5C into their shadow fields and
//! its 0x26 halfword into 0x52 (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");

/// orig: func_800ACF54 (src/BATTLE/BATTLE.PRG/40564.c)
/// Copies the words at 0x5C and 0x1C into 0x60 and 0x4C, the word at 0x5C into 0x1868 unless bit C_0 is set, and 0x26 into 0x52.
pub fn func_800ACF54(arg0: ?*v.D_800F45E0_t) callconv(.c) void {
    const a = arg0.?;
    a.unk60 = psx.byteAt(i32, a, 0x5C).*;
    psx.byteAt(v.func_800ACF54_t, a, 0x4C).* = psx.byteAt(v.func_800ACF54_t, a, 0x1C).*;
    if (psx.getBits(a, "unkC_0") == 0) {
        a.unk1868 = psx.byteAt(i32, a, 0x5C).*;
    }
    a.unk52 = a.unk26;
}
