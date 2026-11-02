//! Camera follow targets for the look-at and eye cameras (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");

/// orig: func_800BE36C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Sets the look-at and eye cameras' follow targets and clears their following flags.
pub fn func_800BE36C(arg0: c_int, arg1: c_int) callconv(.c) void {
    const cam = v.g.D_800F4BA4().*.?;
    cam.lookAt.follow = psx.conv(u8, arg0);
    cam.lookAt.following = 0;
    cam.eye.follow = psx.conv(u8, arg1);
    cam.eye.following = 0;
}
