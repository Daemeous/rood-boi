//! Derives the clip extents and display offsets of an actor record from its position and the box in arg1 (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: func_80092C68 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Computes the per-axis extents at unk24..unk28, clamps them against the box, then writes the display offsets at unk2A..unk32.
pub fn func_80092C68(arg0: ?*v.func_80092F74_t, arg1: ?*v.func_80092F74_t2) callconv(.c) c_int {
    const a = arg0.?;
    const b = arg1.?;
    const span: c_int = b.unk4[1].u16[1];

    const x: c_int = @divTrunc(@as(c_int, a.unkC), 16) -% @as(c_int, b.unk4[0].s16[0]);
    a.unk24 = psx.conv(i16, span -% (if (x >= 0) x else -%x));

    const y: c_int = @divTrunc(@as(c_int, a.unk10), 16) -% @as(c_int, b.unk4[0].s16[1]);
    a.unk26 = psx.conv(i16, span -% (if (y >= 0) y else -%y));

    var z: c_int = @divTrunc(@as(c_int, a.unk14), 16);
    z -%= @as(c_int, b.unk4[1].s16[0]);
    a.unk28 = psx.conv(i16, span -% (if (z >= 0) z else -%z));

    var d: c_int = @as(c_int, a.unk24);
    if (d > 0) {
        z = @as(c_int, a.unkC) -% (@as(c_int, b.unk4[0].s16[0]) << 4);
        if (z < 0) a.unk24 = psx.conv(i16, -%d);
    } else {
        a.unk24 = 0;
    }

    d = @as(c_int, a.unk26);
    if (d > 0) {
        if (@as(c_int, a.unk10) -% (@as(c_int, b.unk4[0].s16[1]) << 4) < 0) a.unk26 = psx.conv(i16, -%d);
    } else {
        a.unk26 = 0;
    }

    d = @as(c_int, a.unk28);
    if (d > 0) {
        if (@as(c_int, a.unk14) -% (@as(c_int, b.unk4[1].s16[0]) << 4) < 0) a.unk28 = psx.conv(i16, -%d);
    } else {
        a.unk28 = 0;
    }

    a.unk24 = psx.conv(i16, @divTrunc(@as(c_int, a.unk24), 4));
    a.unk26 = psx.conv(i16, @divTrunc(@as(c_int, a.unk26), 4));
    a.unk28 = psx.conv(i16, @divTrunc(@as(c_int, a.unk28), 4));
    a.unk2A = psx.conv(i16, @as(c_int, b.unk4[2].u16[0]) +% 1);
    a.unk2C = psx.conv(i16, (@as(c_int, a.unk24) & 0x1F) -% 16);
    a.unk2E = psx.conv(i16, (@as(c_int, a.unk26) & 0x3F) -% 32);
    a.unk30 = 0;
    a.unk32 = 0;
    return 0;
}
