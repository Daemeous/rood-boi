//! Camera step bookkeeping: on the 0x2000 step the eye and look-at points advance by their step vector (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BBE10 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Advances the camera's eye and look-at points on a change to step 0x2000, then records the step.
pub fn func_800BBE10(arg0: c_ushort) callconv(.c) void {
    const cam = v.g.D_800F4BA4().*.?;
    if (arg0 == 0x2000) {
        if (cam.unk1E8 != arg0) {
            _ = c._vectorAdd(
                psx.cptr(c.VECTOR, &cam.eye.current),
                psx.cptr(c.VECTOR, &cam.eye.current),
                psx.cptr(c.VECTOR, &cam.unk1EC),
            );
            _ = c._vectorAdd(
                psx.cptr(c.VECTOR, &cam.lookAt.current),
                psx.cptr(c.VECTOR, &cam.lookAt.current),
                psx.cptr(c.VECTOR, &cam.unk1EC),
            );
        }
    }
    cam.unk1E8 = arg0;
    c.func_800BBE94();
}
