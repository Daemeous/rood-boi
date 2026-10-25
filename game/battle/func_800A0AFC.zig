//! Scales the two bytes of entry arg0 in the D_800F4538 table from arg1 and arg2 (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: func_800A0AFC (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores arg1 and arg2 divided by ONE into entry arg0's bytes, forcing the second to 0xFF when arg1 >= arg2.
pub fn func_800A0AFC(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) void {
    // arg0 is not checked against the table's 17 entries: the C does not check either.
    const temp_a0 = psx.at(v.g.D_800F4538(), arg0).*;
    if (temp_a0) |t| {
        t.unk5BA = psx.conv(u8, @divTrunc(arg1, 4096));
        t.unk5B9 = psx.conv(u8, @divTrunc(arg2, 4096));
        if (arg1 >= arg2) {
            t.unk5B9 = 0xFF;
        }
    }
}
