//! Marks one battle entry's flag byte and reloads its palette on first use
//! (src/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009FD5C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets flag bit (7 - arg2) of entry arg0's flag byte (all bits for -1), reloading its CLUT on first use.
pub fn func_8009FD5C(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) c_int {
    const a3 = psx.at(v.g.D_800F4538(), arg0).*;
    var t0: ?*v.D_800F45E0_t = null;
    var s: *v.D_800F4538_unk64 = undefined;
    if (a3) |actor| {
        s = &actor.unk0.unk64;
    } else {
        t0 = psx.at(v.g.D_800F45E0(), arg0).*;
        const e = t0 orelse return -1;
        s = &e.unk64;
    }

    if (s.unk0 == 0) {
        s.unk1 = 0;
        s.unk2 = psx.conv(u8, (@as(c_int, s.unk2) & 0xFC) | (arg1 & 1));
        if (a3) |actor| {
            const bytes: *align(2) [960]u8 = @alignCast(&actor.unk1424);
            _ = c.vs_main_loadClut(
                @ptrCast(bytes),
                psx.conv(c_uint, @as(c_int, actor.unk0.unkF) +% 0x16),
                0,
                0xA0,
            );
        } else {
            const e = t0.?;
            _ = c.vs_main_loadClut(
                @ptrCast(&e.unk6C[6]),
                0x1F,
                psx.conv(c_uint, @as(c_int, e.unkF) *% 0x10),
                0x10,
            );
        }
    }

    if (arg2 == -1) {
        s.unk0 = 0xFF;
    } else {
        if (psx.conv(c_uint, arg2) >= 8) return -2;
        // arg2 is 0..7 here, so 7 - arg2 is the shift amount 0..7.
        const shift: u5 = @intCast(@as(c_int, 7) -% arg2);
        s.unk0 = psx.conv(u8, @as(c_int, s.unk0) | (@as(c_int, 1) << shift));
    }
    return 0;
}
