//! Sets an object's facing target and turn rate, or recentres its facing (src/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");

/// orig: func_800AA984 (src/BATTLE/BATTLE.PRG/40564.c)
/// Sets object `index`'s facing target to `turn`; `mode` 0 recentres, -1 derives the turn rate from `turn`, any other value is the rate.
pub fn func_800AA984(index: c_int, turn: c_short, mode: c_int) callconv(.c) void {
    const a: *v.D_800F4538_t = psx.at(v.g.D_800F4538(), index).* orelse return;
    var m: c_int = @as(c_int, turn);
    a.unk0.unk3E = psx.conv(i16, m);
    a.unk0.unk3C = 0;
    a.unk0.unk40 = 0;

    // The C's `goto recenter` enters the recentring tail from the -1 branch.
    var recenter = false;
    if (mode == 0) {
        a.unk0.unk18 = 0;
        recenter = true;
    } else if (mode == -1) {
        if (m < 0) m = -%m;
        const speed: c_int = m *% @as(c_int, a.unk5C0);
        a.unk0.unk18 = psx.conv(u16, @divTrunc(speed, 4096));
        if ((speed & 0xFFF) != 0) {
            a.unk0.unk18 = psx.conv(u16, @divTrunc(speed, 4096) +% 1);
        }
        if (a.unk0.unk18 != 0) return;
        recenter = true;
    } else {
        a.unk0.unk18 = psx.conv(u16, mode);
    }

    if (recenter) {
        const sum: i16 = psx.conv(i16, @as(c_int, psx.conv(u16, a.unk0.facing)) +% @as(c_int, a.unk0.unk3E));
        a.unk0.facing = sum;
        a.unk0.facing = psx.conv(i16, @rem(@as(c_int, sum), 4096));
    }
}
