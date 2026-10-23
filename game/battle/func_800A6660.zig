//! Turns a battle object towards a target facing, accumulating the turn or starting one (src/BATTLE/BATTLE.PRG/3A1A0.c).
const psx = @import("common").psx;
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: func_800A6660 (src/BATTLE/BATTLE.PRG/3A1A0.c)
/// Turns object arg0 towards facing arg1 by the shortest way: adds to its pending turn, or starts one when idle.
pub fn func_800A6660(arg0: ?*v.D_800F4538_t, arg1: c_int, arg2: ?*v.func_800A6660_t) callconv(.c) void {
    const obj = arg0.?;
    const u = &obj.unk0;
    const facing: c_int = @as(c_int, u.facing);

    if (arg1 == facing) {
        return;
    }
    if (u.unk18 != 0) {
        if (arg2.?.unk6 == 0) {
            var delta: c_int = arg1 -% ((facing +% @as(c_int, u.unk3E)) & 0xFFF);
            if (delta >= 4096 / 2) {
                delta -%= 4096;
            } else if (delta < -4096 / 2) {
                delta +%= 4096;
            }
            u.unk3E = psx.conv(i16, @as(c_int, u.unk3E) +% delta);
        } else {
            var delta: c_int = arg1 -% facing;
            if (delta >= 4096 / 2) {
                delta -%= 4096;
            } else if (delta < -4096 / 2) {
                delta +%= 4096;
            }
            u.unk3E = psx.conv(i16, delta);
            u.unk18 = 12;
        }
    } else {
        c.func_800AA850(@as(c_int, u.unkF), psx.conv(c_short, arg1), 12);
    }
    if (@as(c_int, obj.unk63C) > 0x80) {
        var delta: c_int = @divTrunc(@as(c_int, u.unk3E), 32);
        if (delta < 0) {
            delta = 0 -% delta;
        }
        if (@as(c_int, u.unk18) < delta) {
            u.unk18 = psx.conv(u16, delta);
        }
    }
}
