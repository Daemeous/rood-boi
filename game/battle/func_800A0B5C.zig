//! Sets two 16-bit fields of a D_800F4538 table entry (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_800A0B5C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets unk5C2 and unk5C0 of table entry arg0 (negative values become 1); an empty entry is left alone.
pub fn func_800A0B5C(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) void {
    // The C indexes D_800F4538 without a bounds check.
    const temp_a0 = psx.at(v.g.D_800F4538(), arg0).* orelse return;

    var value1 = arg1;
    if (value1 < 0) {
        value1 = 1;
    }
    temp_a0.unk5C2 = psx.conv(u16, value1);

    var value2 = arg2;
    if (value2 < 0) {
        value2 = 1;
    }
    temp_a0.unk5C0 = psx.conv(u16, value2);
}
