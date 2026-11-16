//! Battle mode check: sets the mode and camera when the probe reports one, else runs the fallback (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80073898 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets mode 3 and camera 1 when vs_event_pendingEventSelector's low 16 bits are nonzero, else runs vs_event_startPreparedCutscene.
pub fn func_80073898() callconv(.c) void {
    // The C shifts the promoted int left by 16; the shift discards high bits without a check.
    if ((@as(c_int, c.vs_event_pendingEventSelector()) << 16) != 0) {
        v.g.D_800F196C().* = 3;
        v.g._cameraMode().* = 1;
        return;
    }
    c.vs_event_startPreparedCutscene();
}
