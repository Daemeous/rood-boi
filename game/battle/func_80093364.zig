//! Per-tick update of a moving object: advances its position and rotation, wraps the rotation to 12 bits, damps both velocity sets and refreshes its transform (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80093364 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Advances an object by its velocities, wraps its 12-bit rotation, damps both velocity sets and refreshes its transform.
pub fn func_80093364(arg0: ?*v.func_80092F74_t) callconv(.c) c_int {
    const self = arg0.?;

    self.unkC +%= @as(c_int, self.unk24);
    self.unk10 +%= @as(c_int, self.unk26);
    self.unk14 +%= @as(c_int, self.unk28);

    self.unk1C.vx = psx.conv(i16, @as(c_int, self.unk1C.vx) +% @as(c_int, self.unk2C));
    self.unk1C.vy = psx.conv(i16, @as(c_int, self.unk1C.vy) +% @as(c_int, self.unk2E));
    self.unk1C.vz = psx.conv(i16, @as(c_int, self.unk1C.vz) +% @as(c_int, self.unk30));

    self.unk1C.vx = psx.conv(i16, @as(c_int, self.unk1C.vx) & 0xFFF);
    self.unk1C.vy = psx.conv(i16, @as(c_int, self.unk1C.vy) & 0xFFF);
    self.unk1C.vz = psx.conv(i16, @as(c_int, self.unk1C.vz) & 0xFFF);

    if (self.unk2A != 0) {
        const k: c_int = @as(c_int, self.unk2A);
        self.unk24 = psx.conv(i16, @as(c_int, self.unk24) -% @divTrunc(@as(c_int, self.unk24) *% k, 16));
        self.unk26 = psx.conv(i16, @as(c_int, self.unk26) -% @divTrunc(@as(c_int, self.unk26) *% k, 16));
        self.unk28 = psx.conv(i16, @as(c_int, self.unk28) -% @divTrunc(@as(c_int, self.unk28) *% k, 16));
    }

    if (self.unk32 != 0) {
        const k: c_int = @as(c_int, self.unk32);
        self.unk2C = psx.conv(i16, @as(c_int, self.unk2C) -% @divTrunc(@as(c_int, self.unk2C) *% k, 16));
        self.unk2E = psx.conv(i16, @as(c_int, self.unk2E) -% @divTrunc(@as(c_int, self.unk2E) *% k, 16));
        self.unk30 = psx.conv(i16, @as(c_int, self.unk30) -% @divTrunc(@as(c_int, self.unk30) *% k, 16));
    }

    c.func_80092EDC(psx.cptr(c.func_80092F74_t, self));
    return 0;
}
