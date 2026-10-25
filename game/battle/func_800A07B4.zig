//! Reads the unk58 byte of the battle entry in slot `arg0` (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_800A07B4 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies slot `arg0`'s unk58 byte into `arg1`; returns -1 when the slot is empty, else 0.
pub fn func_800A07B4(arg0: c_int, arg1: ?*v.D_800F4538_unk58_2) callconv(.c) c_int {
    // The slot index is not bounds-checked in the C: psx.at indexes without a check.
    const entry = psx.at(v.g.D_800F4538(), arg0).*;
    if (entry == null) {
        return -1;
    }

    arg1.?.* = entry.?.unk0.unk58;
    return 0;
}
