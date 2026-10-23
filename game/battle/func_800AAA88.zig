//! Sets a scene actor's spin speed and the timer that recentres it (src/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");

/// orig: func_800AAA88 (src/BATTLE/BATTLE.PRG/40564.c)
/// Sets slot's spin speed from speed; timer 0 recentres now, -1 derives the time from the speed, any other value is the timer.
pub fn func_800AAA88(slot: c_int, speed: ?*v.SVECTOR, timer: c_int) callconv(.c) void {
    const a: *v.D_800F4538_t = psx.at(v.g.D_800F4538(), slot).*.?;
    const src = speed.?;

    a.unk0.unk3C = src.vx;
    a.unk0.unk3E = src.vy;
    a.unk0.unk40 = src.vz;

    if (timer != 0 and timer != -1) {
        a.unk0.unk18 = psx.conv(u16, timer);
        return;
    }

    if (timer == -1) {
        const ax: c_int = @as(c_int, a.unk0.unk3C);
        const ay: c_int = @as(c_int, a.unk0.unk3E);
        const az: c_int = @as(c_int, a.unk0.unk40);

        var m: c_int = if (ax >= 0) ax else -%ax;
        const my: c_int = if (ay >= 0) ay else -%ay;
        if (m < my) m = my;
        const mz: c_int = if (az >= 0) az else -%az;
        if (m < mz) m = mz;

        m *%= @as(c_int, a.unk5C0);
        a.unk0.unk18 = psx.conv(u16, @divTrunc(m, 4096));
        if ((m & 0xFFF) != 0) {
            a.unk0.unk18 = psx.conv(u16, @divTrunc(m, 4096) +% 1);
        }

        // unk18 is read back after the u16 store, as the C reads it.
        if (a.unk0.unk18 != 0) return;
    } else {
        a.unk0.unk18 = 0;
    }

    // recenter:
    a.unk0.pitch = psx.conv(i16, @as(c_int, a.unk0.pitch) +% @as(c_int, a.unk0.unk3C));
    a.unk0.facing = psx.conv(i16, @as(c_int, a.unk0.facing) +% @as(c_int, a.unk0.unk3E));
    a.unk0.roll = psx.conv(i16, @as(c_int, a.unk0.roll) +% @as(c_int, a.unk0.unk40));

    a.unk0.pitch = psx.conv(i16, @as(c_int, a.unk0.pitch) & 0xFFF);
    a.unk0.facing = psx.conv(i16, @as(c_int, a.unk0.facing) & 0xFFF);
    a.unk0.roll = psx.conv(i16, @as(c_int, a.unk0.roll) & 0xFFF);
}
