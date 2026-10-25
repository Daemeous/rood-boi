//! Sets a battle record's flag byte and, when the flag is nonzero, copies two
//! records into it (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: func_8009F940 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets the flag of the record at `index`; when `flag` is nonzero, copies `first` and `second` into it.
pub fn func_8009F940(index: c_int, flag: c_int, first: ?*v.D_800F4538_unk58_2, second: ?*v.D_800F4538_unk58_2) callconv(.c) void {
    const temp = psx.at(v.g.D_800F4538(), index).*;
    if (temp) |t| {
        t.unk17FD = psx.conv(u8, flag);
        if (flag != 0) {
            @as(*align(1) c_int, @ptrCast(&t.unk6F4)).* = @as(*align(1) const c_int, @ptrCast(first.?)).*;
            t.unk6F8 = second.?.*;
        }
    }
}
