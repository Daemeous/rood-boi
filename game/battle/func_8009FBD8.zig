//! Sets the unk6E5 byte of a battle slot's record (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_8009FBD8 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets the unk6E5 byte of slot `arg0` to `arg1` (13 when `arg1` is -1); -1 if the slot is empty.
pub fn func_8009FBD8(arg0: c_int, arg1: c_int) callconv(.c) c_int {
    // The C indexes D_800F4538 without a bounds check: psx.at keeps that.
    const slot: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), arg0).*;
    const actor = slot orelse return -1;
    actor.unk6E5 = if (arg1 == -1) 0xD else psx.conv(u8, arg1);
    return 0;
}
