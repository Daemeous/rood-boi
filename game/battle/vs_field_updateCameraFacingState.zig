//! First-person view bookkeeping in battle (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_updateCameraFacingState (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Syncs the camera angles from the position, then copies the first-person view flag into bit 16 of the scratch facing.
pub fn vs_field_updateCameraFacingState() callconv(.c) void {
    _ = c.vs_battle_syncCameraAnglesFromPosition(null);
    const view = v.g.vs_view();
    const enabled: u32 = @bitCast(v.g._firstPersonViewEnabled().*);
    view.firstPersonViewFacing = (view.firstPersonViewFacing & 0x7FFF) | (enabled << 0x10);
}
