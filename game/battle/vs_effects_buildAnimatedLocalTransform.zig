//! Builds a moving part's matrix each frame: rotate, translate and scale from lerped
//! parameters, or clear the translation (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_effects_buildAnimatedLocalTransform (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Lerps a part's rotation, translation and scale into its matrix when arg0.unk0 bit 0 is set; otherwise resets the matrix and zeroes its translation.
pub fn vs_effects_buildAnimatedLocalTransform(arg0: ?*v.func_800D0B30_t, arg1: ?*v.SVECTOR, arg2: ?*v.func_800D0B30_t2) callconv(.c) void {
    const desc = arg0.?;
    const work = arg2.?;

    if ((desc.unk0 & 1) != 0) {
        const vec = arg1.?; // the C touches arg1 on this path only
        c.vs_battle_lerpSvector(@ptrCast(&desc.unk14), v.g.vs_effects_curveSamples()[desc.unk6], psx.cptr(c.SVECTOR, &work.unk8));
        vec.vx = psx.conv(i16, @as(c_int, vec.vx) +% work.unk8.vx);
        vec.vy = psx.conv(i16, @as(c_int, vec.vy) +% work.unk8.vy);
        vec.vz = psx.conv(i16, @as(c_int, vec.vz) +% work.unk8.vz);
        _ = c.RotMatrix_gte(psx.cptr(c.SVECTOR, vec), psx.cptr(c.MATRIX, &work.unk48));
        c.vs_battle_lerpVector(@ptrCast(&desc.unk20), v.g.vs_effects_curveSamples()[desc.unk4], psx.cptr(c.VECTOR, &work.unk10));
        _ = c.TransMatrix(psx.cptr(c.MATRIX, &work.unk48), psx.cptr(c.VECTOR, &work.unk10));
        c.vs_battle_lerpVector(@ptrCast(&desc.unk2C), v.g.vs_effects_curveSamples()[desc.unk7], psx.cptr(c.VECTOR, &work.unk20));
        _ = c.ScaleMatrix(psx.cptr(c.MATRIX, &work.unk48), psx.cptr(c.VECTOR, &work.unk20));
        return;
    }

    _ = c.vs_core_mathIdentityRotation(@ptrCast(&work.unk48));
    work.unk48.t[2] = 0;
    work.unk48.t[1] = 0;
    work.unk48.t[0] = 0;
}
