//! Advances a falling object one tick: gravity, drag, floor contact and bounce
//! (src/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_8009306C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Advances the motion one tick; returns 0 once it has come to rest on the floor, else 1.
pub fn func_8009306C(motion: ?*v.func_80092F74_t) callconv(.c) c_int {
    const m = motion.?;

    m.unkC +%= @as(c_int, m.unk24);
    m.unk10 +%= @as(c_int, m.unk26);
    m.unk14 +%= @as(c_int, m.unk28);
    m.unk1C.vx = psx.conv(i16, @as(c_int, m.unk1C.vx) +% @as(c_int, m.unk2C));
    m.unk1C.vy = psx.conv(i16, @as(c_int, m.unk1C.vy) +% @as(c_int, m.unk2E));
    m.unk1C.vz = psx.conv(i16, @as(c_int, m.unk1C.vz) +% @as(c_int, m.unk30));
    m.unk1C.vx = psx.conv(i16, @as(c_int, m.unk1C.vx) & 0xFFF);
    m.unk1C.vy = psx.conv(i16, @as(c_int, m.unk1C.vy) & 0xFFF);
    m.unk1C.vz = psx.conv(i16, @as(c_int, m.unk1C.vz) & 0xFFF);

    // The low 15 bits hold the signed floor height; bit 15 is a collision flag.
    var height: c_int = @as(c_int, c.func_8008DC7C(@divTrunc(m.unkC, 16), @divTrunc(m.unk14, 16))) << 17;
    height >>= 17;
    if (height < @divTrunc(m.unk10, 16)) {
        m.unk1C.pad +%= 1;
        if (m.unk1C.pad < 3 and m.unk2A > 0) {
            const a2: c_int = @as(c_int, m.unk2A);
            const absA: c_int = if (a2 >= 0) a2 else -%a2;
            const velocity: c_int = @divTrunc(@as(c_int, m.unk26), -%(absA +% 2));

            m.unk10 = height << 4;
            m.unk24 = psx.conv(i16, @divTrunc(@as(c_int, m.unk24), 2));
            m.unk28 = psx.conv(i16, @divTrunc(@as(c_int, m.unk28), 2));
            m.unk2C = psx.conv(i16, @divTrunc(@as(c_int, m.unk2C), 2));
            m.unk2E = psx.conv(i16, @divTrunc(@as(c_int, m.unk2E), 2));
            m.unk30 = psx.conv(i16, @divTrunc(@as(c_int, m.unk30), 2));
            m.unk26 = psx.conv(i16, velocity);
        } else {
            m.unk1C.pad = 0;
            return 0;
        }
    }

    if (m.unk2A != 0) {
        m.unk24 = psx.conv(i16, @as(c_int, m.unk24) -% @divTrunc(@as(c_int, m.unk24) *% @as(c_int, m.unk2A), 16));
        m.unk28 = psx.conv(i16, @as(c_int, m.unk28) -% @divTrunc(@as(c_int, m.unk28) *% @as(c_int, m.unk2A), 16));
    }
    m.unk26 = psx.conv(i16, @as(c_int, m.unk26) +% 16);

    if (m.unk32 != 0) {
        m.unk2C = psx.conv(i16, @as(c_int, m.unk2C) -% @divTrunc(@as(c_int, m.unk2C) *% @as(c_int, m.unk32), 16));
        m.unk2E = psx.conv(i16, @as(c_int, m.unk2E) -% @divTrunc(@as(c_int, m.unk2E) *% @as(c_int, m.unk32), 16));
        m.unk30 = psx.conv(i16, @as(c_int, m.unk30) -% @divTrunc(@as(c_int, m.unk30) *% @as(c_int, m.unk32), 16));
    }

    c.func_80092EDC(psx.cptr(c.func_80092F74_t, m));
    return 1;
}
