//! Copies the eight halfwords of arg1's four-word table into arg0's fields at 0x24..0x32 (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: func_80092E14 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Copies the eight halfwords of arg1's four-word table into arg0's fields at 0x24..0x32.
pub fn func_80092E14(arg0: ?*v.func_80092F74_t, arg1: ?*v.func_80092F74_t2) callconv(.c) c_int {
    const dst = arg0.?;
    const src = arg1.?;
    dst.unk24 = psx.conv(i16, src.unk4[0].u16[0]);
    dst.unk26 = psx.conv(i16, src.unk4[0].u16[1]);
    dst.unk28 = psx.conv(i16, src.unk4[1].u16[0]);
    dst.unk2A = psx.conv(i16, src.unk4[1].u16[1]);
    dst.unk2C = psx.conv(i16, src.unk4[2].u16[0]);
    dst.unk2E = psx.conv(i16, src.unk4[2].u16[1]);
    dst.unk30 = psx.conv(i16, src.unk4[3].u16[0]);
    dst.unk32 = psx.conv(i16, src.unk4[3].u16[1]);
    return 0;
}
