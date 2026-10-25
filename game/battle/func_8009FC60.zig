//! Sets the position and pad word of battle object `arg0`'s slot record
//! (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009FC60 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores `arg1` and a position (the given vector, else the record's own offsets) in object `arg0`'s record, then either settles it (`arg3 == -1`) or sets its pad word to `arg3`.
pub fn func_8009FC60(arg0: c_int, arg1: c_int, arg2: ?*v.SVECTOR, arg3: c_int) callconv(.c) c_int {
    // arg0 is not checked against the array's bound: the C indexes without one.
    const temp_s0: *v.D_800F4538_t = psx.at(v.g.D_800F4538(), arg0).* orelse return -1;

    if (c.func_800A152C(arg0, 2, 3) < 0) {
        return -2;
    }

    temp_s0.unk17FC = psx.conv(i8, arg1);
    if (arg2) |pos| {
        temp_s0.unk17F4 = pos.*;
    } else {
        temp_s0.unk17F4.vx = psx.conv(i16, temp_s0.unk0.unk16);
        temp_s0.unk17F4.vy = psx.conv(i16, temp_s0.unk0.unk14);
    }
    if (arg3 == -1) {
        c.func_8009FD38(@ptrCast(temp_s0));
    } else {
        temp_s0.unk17F4.pad = psx.conv(i16, arg3);
    }
    return 0;
}
