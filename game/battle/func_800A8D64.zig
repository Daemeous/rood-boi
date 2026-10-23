//! Room section lookup for a point: the section of its 128-unit cell, if the point lies on its live side
//! (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: func_800A8D64 (src/BATTLE/BATTLE.PRG/40564.c)
/// Returns the room section of the 128-unit cell at a point when the point is on that section's live side, else null.
pub fn func_800A8D64(arg0: ?*v.SVECTOR, arg1: c_int) callconv(.c) ?*v._mpdRoomSection3 {
    const pos = arg0.?;
    var temp_s1: c_int = @divTrunc(@as(c_int, pos.vx), 128);
    var temp_s0: c_int = @divTrunc(@as(c_int, pos.vz), 128);
    const found: *v._mpdRoomSection3 = @ptrCast(c.func_8008B764(
        psx.conv(c_uint, temp_s1),
        psx.conv(c_uint, temp_s0),
        arg1,
    ) orelse return null);

    temp_s1 = @as(c_int, pos.vx) -% (temp_s1 << 7);
    temp_s0 = @as(c_int, pos.vz) -% (temp_s0 << 7);

    // Values 6 and 7 of the 3-bit field match no case; the C falls off the end and
    // returns whatever is in the return register. Taken here as the found section.
    switch (@as(c_int, psx.getBits(found, "unk0_10"))) {
        0 => return found,
        1 => {
            if (temp_s1 +% temp_s0 > 0x80) return null;
            return found;
        },
        2 => {
            if (temp_s1 -% temp_s0 > 0) return null;
            return found;
        },
        3 => {
            if (temp_s1 +% temp_s0 < 0x80) return null;
            return found;
        },
        4 => {
            if (temp_s1 -% temp_s0 < 0) return null;
            return found;
        },
        5 => return null,
        else => return found,
    }
}
