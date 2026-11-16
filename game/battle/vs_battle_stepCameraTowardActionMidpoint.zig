//! Gets a stack vector from vs_battle_actionCameraMidpoint and steps it toward its target (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_stepCameraTowardActionMidpoint (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Gets a stack vector from vs_battle_actionCameraMidpoint and, when `step` is nonzero, steps it toward its target.
pub fn vs_battle_stepCameraTowardActionMidpoint(step: c_int) callconv(.c) void {
    var sp10: v.SVECTOR = undefined;
    c.vs_battle_actionCameraMidpoint(psx.cptr(c.SVECTOR, &sp10));
    if (step != 0) {
        _ = c._stepTowardTarget(psx.cptr(c.SVECTOR, &sp10), 1, 0x28);
    }
}
