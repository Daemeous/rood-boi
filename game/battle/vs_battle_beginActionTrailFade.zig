//! Camera mode reset and the first action slot's target (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_beginActionTrailFade (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the camera mode to 10, then runs func_8006C4A4 on the first action slot's target with the result of func_800A152C.
pub fn vs_battle_beginActionTrailFade() callconv(.c) void {
    v.g._cameraMode().* = 10;
    const base: *v.D_800F19CC_t = v.g.vs_battle_actionState().*.?;
    const temp: *v.D_800F19CC_t2 = &base.queue[0];
    // The C's argument order: the nested call is evaluated first, then the outer target is read.
    const result = c.func_800A152C(@as(c_int, temp.primaryTarget.target.targetActor), 0xF0, 0);
    c.func_8006C4A4(@as(c_int, temp.primaryTarget.target.targetActor), result);
}
