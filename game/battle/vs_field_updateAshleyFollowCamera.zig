//! Aims the battle camera at the position reported by vs_actors_actorPlacement (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_updateAshleyFollowCamera (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Moves the battle camera's look-at point to the position vs_actors_actorPlacement returns, then updates its angles.
pub fn vs_field_updateAshleyFollowCamera() callconv(.c) c_int {
    if (v.g.D_800F1968().* == 0) {
        c.vs_actors_updateRoomActorMovement();
    }

    var sp10: v.func_8006EBF8_t = undefined;
    _ = c.vs_actors_actorPlacement(0, &sp10);

    // Positions are 4.12 fixed point: each component is shifted left by 12.
    // The y offset is the C's constant 0xFFFA6000, i.e. -0x5A000 as an int.
    var position: v.VECTOR = undefined;
    position.vx = @as(c_int, sp10.location.position.vx) << 12;
    position.vy = (@as(c_int, sp10.location.position.vy) << 12) +% -0x5A000;
    position.vz = @as(c_int, sp10.location.position.vz) << 12;

    c._snapLookAtToPosition(psx.cptr(c.VECTOR, &position));
    c._setCameraPositionFromAngles(psx.cptr(c._sphericalValues, &v.g.vs_battle_cameraCurrentSpherical().values));

    // The original returns `v`, which it never assigns: uninitialised in the original; returns 0.
    return 0;
}
