//! Tests whether a point is a valid placement: its height clears the minimum and the flag bit is clear (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c).
const std = @import("std");
const v = @import("views_3A1A0");
const c = @import("c_3A1A0");

/// orig: vs_field_canPlaceGimmickAtPoint (pc/src/game/BATTLE/BATTLE.PRG/3A1A0.c)
/// Returns 1 when the point at (x, z) has a height of at least minimumHeight, its floor's bit 20 (vs_field_collisionProbeTile) is clear and no actor stands on its tile (vs_field_findActorOccupyingTile), else 0.
pub fn vs_field_canPlaceGimmickAtPoint(x: c_int, z: c_int, minimumHeight: c_int) callconv(.c) c_int {
    var offset = std.mem.zeroes(c.SVECTOR);
    const height = c.vs_field_sampleActorSupportHeight(&offset, x, z, 1);
    if (height < minimumHeight) return 0;
    const flags = v.g.vs_field_collisionProbeTile().*.?[0];
    if (((flags >> 20) & 1) != 0) return 0;
    return @intFromBool(c.vs_field_findActorOccupyingTile(@divTrunc(x, 128), @divTrunc(z, 128), height, 1) == null);
}
