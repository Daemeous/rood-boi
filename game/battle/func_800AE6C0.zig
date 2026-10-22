//! Plays the sound effect for a source actor, with its pan, and an optional second copy (src/BATTLE/BATTLE.PRG/44F14.c).
const psx = @import("common").psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: func_800AE6C0 (src/BATTLE/BATTLE.PRG/44F14.c)
/// Plays sound `sfx` for `owner`'s slot, panned by its position, and `altSfx` too when nonzero.
pub fn func_800AE6C0(arg0: ?*v.D_800F4538_t, arg1: c_int, arg2: c_int) callconv(.c) void {
    // Array index is unchecked: the C indexes D_800F4538 by a u8.
    const temp: *v.D_800F4538_t = psx.at(v.g.D_800F4538(), @as(c_uint, arg0.?.unk0.unkF)).*.?;
    var sp10: [2]c_int = undefined;

    if (temp.unk17E4.unk0 == 2) {
        sp10[0] = 0x80;
        sp10[1] = 0;
    } else {
        if (psx.getBits(&temp.unk0, "unk9_0") != 0 or
            psx.getBits(&temp.unk0, "unk9_4") != 0 or
            psx.getBits(&temp.unk0, "unk9_7") == 0 or
            psx.getBits(&temp.unk0, "unkA_0") != 0 or
            psx.getBits(&temp.unk0, "unkA_3") != 0 or
            psx.getBits(&temp.unk0, "unkA_5") != 0)
        {
            _ = c.func_800A1280(@as(c_int, arg0.?.unk0.unkF), @as(c_int, 0xFF), @ptrCast(&temp.unk6FC), @as(c_int, 0));
        }

        const temp_v0: c_int = c.vs_main_computeSfxPan(
            psx.byteAt(c_int, &temp.unk6FC, 0).*,
            @as(c_int, temp.unk6FC.vz),
        );
        sp10[0] = temp_v0 >> 16;

        if (temp.unk17E4.unk1 != 0) {
            sp10[0] = @as(c_int, temp.unk17E4.unk1);
        }

        sp10[1] = temp_v0 & 0xFFFF;

        if (temp.unk17E4.unk2 != 0) {
            sp10[1] = @as(c_int, temp.unk17E4.unk2);
        }
    }

    if (sp10[1] != 0) {
        c.vs_main_playSfx(@as(c_int, 0x7E), @as(c_long, arg1), sp10[0], sp10[1]);
        if (arg2 != 0) {
            c.vs_main_playSfx(@as(c_int, 0x7E), @as(c_long, arg2), sp10[0], sp10[1]);
        }
    }
}
