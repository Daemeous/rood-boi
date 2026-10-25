//! Looks up a per-slot battle record's byte at offset 0x6E5 (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_8009FC20 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores slot `index`'s byte at 0x6E5 into `out`; returns -1 for an empty slot, else 0.
pub fn func_8009FC20(arg0: c_int, arg1: [*c]c_int) callconv(.c) c_int {
    // The C does not bound the index: psx.at reproduces the unchecked element address.
    const temp_a0: ?*v.D_800F4538_t = psx.at(v.g.D_800F4538(), arg0).*;

    if (temp_a0 == null) {
        return -1;
    }

    arg1[0] = @as(c_int, temp_a0.?.unk6E5);

    return 0;
}
