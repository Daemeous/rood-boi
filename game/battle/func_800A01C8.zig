//! Sets three bytes of a battle record selected by index (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_800A01C8 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores three bytes into the record at slot `index`; returns -1 if that slot is empty.
pub fn func_800A01C8(index: c_int, value0: c_int, value1: c_int, value2: c_int) callconv(.c) c_int {
    // The C indexes D_800F4538 without a bounds check: psx.at keeps that.
    const record: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), index).*;
    if (record == null) {
        return -1;
    }
    const rec = record.?;

    rec.unk6E0 = psx.conv(u8, value0);
    rec.unk6E1 = psx.conv(u8, value1);
    rec.unk6E2 = psx.conv(u8, value2);

    return 0;
}
