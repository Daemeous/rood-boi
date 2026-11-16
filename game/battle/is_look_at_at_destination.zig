//! Whether the camera's look-at point has reached its destination (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: _isLookAtAtDestination (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns 1 when the camera's look-at point, clamped to the zone, matches the stored look-at point at 4096 granularity.
pub fn isLookAtAtDestination() callconv(.c) c_int {
    var sp10: v.func_80077DF0_t = undefined;
    var sp30: v.func_8006EBF8_t = undefined;

    _ = c.vs_actors_actorPlacement(0, &sp30);
    sp10.position.vx = @as(c_int, sp30.location.position.vx) *% 4096;
    const vyScaled: c_int = @as(c_int, sp30.location.position.vy) *% 4096;
    // The sum is unsigned in C (0xFFFA6000 is an unsigned int), then stored back as int.
    sp10.position.vy = psx.conv(c_int, psx.conv(c_uint, vyScaled) +% 0xFFFA6000);
    sp10.position.vz = @as(c_int, sp30.location.position.vz) *% 4096;
    if (sp10.position.vy > 0) {
        sp10.position.vy = 0;
    }
    c._clampPositionToZoneBounds(psx.cptr(c.VECTOR, &sp10.position), psx.cptr(c.VECTOR, &sp10.position));
    sp10.lookAt = v.g.vs_view().camera.lookAt;

    if ((sp10.position.vx == (sp10.lookAt.vx & ~@as(c_int, 0xFFF))) and
        (sp10.position.vz == (sp10.lookAt.vz & ~@as(c_int, 0xFFF))) and
        (sp10.position.vy == (sp10.lookAt.vy & ~@as(c_int, 0xFFF))))
    {
        return 1;
    }
    return 0;
}
