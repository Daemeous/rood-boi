//! Copies a four-word vector message into the function's state, then runs func_80092EDC (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: func_80092E7C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Copies three words and a short vector from `src` into `dst`, then calls func_80092EDC on `dst`.
pub fn func_80092E7C(arg0: ?*v.func_80092F74_t, arg1: ?*v.func_80092F74_t2) callconv(.c) c_int {
    const dst = arg0.?;
    const src = arg1.?;
    dst.unkC = src.unk4[0].s32;
    dst.unk10 = src.unk4[1].s32;
    dst.unk14 = src.unk4[2].s32;
    dst.unk1C.vx = psx.conv(i16, src.unk4[3].s32);
    dst.unk1C.vy = psx.conv(i16, src.unk14);
    dst.unk1C.vz = psx.conv(i16, src.unk18);
    c.func_80092EDC(psx.cptr(c.func_80092F74_t, dst));
    return 0;
}
