//! Copies three state bytes of a battle entry out to the caller (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_800A071C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies bytes 0x6F2, 0x6F1 and 0x6F0 of entry `slot` into `out`; returns -1 when the slot is empty.
pub fn func_800A071C(slot: c_int, out: ?*v.D_800F4538_unk58_2) callconv(.c) c_int {
    // The C indexes D_800F4538 without a bounds check: psx.at keeps that.
    const entry = psx.at(v.g.D_800F4538(), slot).*;
    if (entry) |e| {
        const o = out.?;
        o.unk0 = e.unk6F2;
        o.unk1 = e.unk6F1;
        o.unk2 = e.unk6F0;
        return 0;
    }
    return -1;
}
