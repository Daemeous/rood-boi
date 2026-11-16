//! Battle step check: whether the stored target vector has been reached (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_waitForChestCameraFocus (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns 0 when the stored target is reached and vs_actors_actorStateFlags(0) has bit 27 set, else 1.
pub fn vs_field_waitForChestCameraFocus() callconv(.c) c_int {
    const src = &v.g.vs_field_interactionFocus().unk4.location.position;
    // The C leaves sp10.pad unset; the object's stack holds 0 there, so start it at 0.
    var sp10: v.SVECTOR = .{ .vx = 0, .vy = 0, .vz = 0, .pad = 0 };

    sp10.vx = src.vx;
    sp10.vy = src.vy;
    sp10.vz = src.vz;

    if ((c._stepTowardTarget(psx.cptr(c.SVECTOR, &sp10), 0, 0x28) == 0) and
        ((c.vs_actors_actorStateFlags(0) & 0x08000000) != 0))
    {
        return 0;
    }
    return 1;
}
