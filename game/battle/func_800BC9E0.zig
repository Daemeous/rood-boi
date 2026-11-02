//! The event camera's frame update (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// D_800F4BA4, read again for every statement as the C does (its stores and calls may change it).
fn camera() *v.D_800F4BA4_t {
    return v.g.D_800F4BA4().*.?;
}

/// orig: func_800BC9E0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Each point targets the other's current position, then the look-at and eye moves step
/// (func_800BCA8C), the roll and unk174 (func_800BD57C), the projection and clip ramps
/// (func_800BDF6C) and func_800BE180.
pub fn func_800BC9E0() callconv(.c) void {
    camera().eye.target.vx = camera().lookAt.current.vx;
    camera().eye.target.vy = camera().lookAt.current.vy;
    camera().eye.target.vz = camera().lookAt.current.vz;
    camera().lookAt.target.vx = camera().eye.current.vx;
    camera().lookAt.target.vy = camera().eye.current.vy;
    camera().lookAt.target.vz = camera().eye.current.vz;
    var cam = camera();
    c.func_800BCA8C(psx.cptr(c.D_800F4BA4_t2, &cam.lookAt), psx.cptr(c.D_800F4BA4_t2, &cam.eye));
    cam = camera();
    c.func_800BCA8C(psx.cptr(c.D_800F4BA4_t2, &cam.eye), psx.cptr(c.D_800F4BA4_t2, &cam.lookAt));
    c.func_800BD57C(psx.cptr(c.func_800BD57C_t, &camera().unk168));
    c.func_800BD57C(psx.cptr(c.func_800BD57C_t, &camera().unk174));
    c.func_800BDF6C(psx.cptr(c.func_800BDF6C_unk180_t, &camera().projectionDistance));
    c.func_800BDF6C(psx.cptr(c.func_800BDF6C_unk180_t, &camera().nearClip));
    c.func_800BDF6C(psx.cptr(c.func_800BDF6C_unk180_t, &camera().farClip));
    c.func_800BE180();
}
