//! Applies a script's 5-byte scroll command to the D_800F4B88 record
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B9FC0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the scroll record at once, or stores its previous values and steps it towards the new target.
pub fn func_800B9FC0(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    _ = c.func_80090C2C(@as(c_int, arg0[1] >> 4));
    const s = v.g.D_800F4B88();
    if (arg0[4] == 0) {
        s.unk10 = psx.conv(i16, @as(c_int, arg0[1] & 0xF));
        c.func_80091320(@as(c_int, s.unk10));
        s.unk4 = psx.conv(i16, @as(c_int, arg0[2]) *% 16);
        s.unkA = psx.conv(i16, @as(c_int, arg0[3]) *% 4 +% 512);
        c.func_8009134C(@as(c_int, s.unk4), @as(c_int, s.unkA));
    } else {
        const vv: c_int = @as(c_int, arg0[1] & 0xF);

        s.unk12 = s.unk10;
        s.unk14 = psx.conv(i16, vv -% @as(c_int, s.unk10));
        s.unkC = s.unkA;
        const base: i16 = psx.conv(i16, @as(c_int, s.unkC) -% 512);
        s.unkE = psx.conv(i16, @as(c_int, arg0[3]) *% 4 -% @as(c_int, base));
        s.unk6 = s.unk4;
        var delta: c_int = @as(c_int, arg0[2]) *% 16 -% @as(c_int, s.unk6);
        if (delta < -2048) {
            delta +%= 4096;
        } else if (delta > 2048) {
            delta -%= 4096;
        }
        s.unk8 = psx.conv(i16, delta);
        s.unk3 = arg0[4];
        s.unk2 = 0;
    }
    return 0;
}
