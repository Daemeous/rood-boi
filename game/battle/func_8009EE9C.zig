//! Updates the drawn-weapon stance of one battle actor slot (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009EE9C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Aims the drawn weapon of actor slot `arg0` at `arg1`, clears the stance when it is not held, then runs the trailing update.
pub fn func_8009EE9C(arg0: c_int, arg1: ?*v.SVECTOR, _: c_int) callconv(.c) void {
    const temp_s0 = psx.at(v.g.D_800F4538(), arg0).*.?;

    if (psx.getBits(&temp_s0.unk0, "weaponDrawn") != 0 and
        (temp_s0.unk0.unkF == 0 or
            (psx.getBits(&temp_s0.unk0, "unkA_5") == 0 and
                psx.getBits(&temp_s0.unk0, "unkA_6") == 0 and
                psx.getBits(&temp_s0.unk0, "unkA_0") != 2 and
                psx.getBits(&temp_s0.unk0, "unkA_0") != 3)))
    {
        var var_s1: c_int = 4;

        if (@as(c_int, temp_s0.unk63C) < 0x80) {
            temp_s0.unk0.unk3E = psx.conv(i16, c.func_8009E180(@ptrCast(temp_s0), @ptrCast(arg1)));
            temp_s0.unk0.unk18 = 4;
            var_s1 = 8;
        }

        const temp_v0: c_int = c.func_8009E228(@ptrCast(temp_s0), @ptrCast(arg1));
        var var_a1: c_int = 0x4F;

        if (temp_v0 >= -0xE4) {
            var_a1 = 0x50;
            if (temp_v0 >= 0xAC) {
                var_a1 = 0x51;
            }
        }

        _ = c.func_800A0204(arg0, var_a1, 0, var_s1);
        c.func_8009FD38(@ptrCast(temp_s0));
    }

    if (temp_s0.unk0.unkF == 0) {
        if (psx.getBits(&temp_s0.unk0, "unkA_0") != 3) {
            psx.setBits(&temp_s0.unk0, "unkA_0", 0);
        }
        temp_s0.unk0.unk11 = 0;
    }

    _ = c.func_8006C84C(arg0);
}
