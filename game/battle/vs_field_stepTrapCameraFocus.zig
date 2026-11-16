//! Steps a scratch vector toward a target (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_stepTrapCameraFocus (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Copies the vector at vs_field_interactionFocus's unk4 into a local and steps it toward a target with arguments 0 and 0x28.
pub fn vs_field_stepTrapCameraFocus() callconv(.c) c_int {
    // The C leaves the local's pad field uninitialised; only vx, vy and vz are copied.
    var sp10: v.SVECTOR = undefined;
    const src = &v.g.vs_field_interactionFocus().unk4.location.position;
    sp10.vx = psx.conv(i16, psx.conv(u16, src.vx));
    sp10.vy = psx.conv(i16, psx.conv(u16, src.vy));
    sp10.vz = psx.conv(i16, psx.conv(u16, src.vz));
    return c._stepTowardTarget(psx.cptr(c.SVECTOR, &sp10), 0, 0x28);
}
