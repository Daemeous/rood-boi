//! Stores three bytes of a battle record in the D_800F4538 table slot `arg0`
//! (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_800A0768 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies three bytes of `arg1` into the record at slot `arg0`; -1 if the slot is empty.
pub fn func_800A0768(arg0: c_int, arg1: ?*v.D_800F4538_unk58_2) callconv(.c) c_int {
    // The C does not check arg0 against the 17 slots: psx.at computes the address as C does.
    const temp_a0 = psx.at(v.g.D_800F4538(), arg0).*;

    if (temp_a0) |t| {
        t.unk6F2 = arg1.?.unk0;
        t.unk6F1 = arg1.?.unk1;
        t.unk6F0 = arg1.?.unk2;
        return 0;
    }
    return -1;
}
