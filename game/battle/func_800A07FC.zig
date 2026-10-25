//! Sets or clears the 0x5AC bit 8 flag of entry `index` in the D_800F4538 table, copying its 4-byte record in when given one (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_800A07FC (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets the flag on table entry `arg0` and copies `arg1` into its unk58 record, or clears the flag when `arg1` is null; -1 if the entry is empty.
pub fn func_800A07FC(arg0: c_int, arg1: ?*v.D_800F4538_unk58_2) callconv(.c) c_int {
    // The C does not bounds-check arg0 against the 17-entry table.
    const temp_a2 = psx.at(v.g.D_800F4538(), arg0).* orelse return -1;

    if (arg1) |src| {
        psx.setBits(temp_a2, "unk5AC_8", 1);
        temp_a2.unk0.unk58 = src.*;
    } else {
        psx.setBits(temp_a2, "unk5AC_8", 0);
    }

    return 0;
}
