//! Fires a weapon-drawn actor's attack at its target and clears its idle state (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009DDF4 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Runs the weapon-drawn attack of actor `arg0` against `arg1`, then clears its idle flags.
pub fn func_8009DDF4(arg0: c_int, arg1: ?*v.SVECTOR) callconv(.c) void {
    // ROOD_PC: an empty actor slot reads through NULL, which maps to PS1 address 0 at 0x80000000.
    const slot = psx.at(v.g.D_800F4538(), arg0).*;
    const temp: *align(1) v.D_800F4538_t = if (slot) |p| p else psx.lowRam(v.D_800F4538_t);
    const hdr: *align(1) v.D_800F4538_unk0 = &temp.unk0;

    if (psx.getBits(hdr, "weaponDrawn") != 0 and
        (hdr.unkF == 0 or
            (psx.getBits(hdr, "unkA_5") == 0 and psx.getBits(hdr, "unkA_6") == 0 and
                psx.getBits(hdr, "unkA_0") != 2 and psx.getBits(hdr, "unkA_0") != 3)))
    {
        var var_s1: c_int = 4;

        if (temp.unk63C < 0x80) {
            hdr.unk3E = psx.conv(i16, c.func_8009E180(@ptrCast(temp), @ptrCast(arg1)));
            hdr.unk18 = 4;
            var_s1 = 8;
        }

        const temp_v0: c_int = c.func_8009E228(@ptrCast(temp), @ptrCast(arg1));
        var var_a1: c_int = 0x4F;

        if (temp_v0 >= -0xE4) {
            var_a1 = 0x50;
            if (temp_v0 >= 0xAC) {
                var_a1 = 0x51;
            }
        }

        _ = c.func_800A0204(arg0, var_a1, 0, var_s1);
        c.func_8009FD38(@ptrCast(temp));
    }

    if (hdr.unkF == 0) {
        if (psx.getBits(hdr, "unkA_0") != 3) {
            psx.setBits(hdr, "unkA_0", 0);
        }
        hdr.unk11 = 0;
    }
}
