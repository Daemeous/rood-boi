//! Tile occupancy counts (D2: 7E05C.s; oracle pc/src/battle_movement.c).
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: tile_occupancy (pc/src/battle_movement.c)
/// Adds `delta` (in steps of 0x400) to the count of the D_800F58D0 cell under
/// the actor's placement word (0x5C); nothing when D_800F58B8 is null (the
/// placement is read first, as in the original).
fn tileOccupancy(actor: [*]u8, delta: c_int) void {
    const placement: c_uint = psx.byteAt(c_uint, actor, 0x5c).*;
    if (v.g.D_800F58B8().* == 0) return;
    const address = v.g.D_800F58D0().* +% (placement & 0xff) *% 2 +% ((placement << 8) >> 18);
    const cell: *align(1) u16 = @ptrFromInt(address);
    cell.* = psx.conv(u16, @as(c_int, cell.*) +% (delta << 10));
}

/// orig: func_800E6898 (pc/src/battle_movement.c)
/// Counts one more actor on the tile under `actor`.
pub fn addTileOccupancy(actor: [*]u8) callconv(.c) void {
    tileOccupancy(actor, 1);
}

/// orig: func_800E68A0 (pc/src/battle_movement.c)
/// Counts one less actor on the tile under `actor`.
pub fn removeTileOccupancy(actor: [*]u8) callconv(.c) void {
    tileOccupancy(actor, -1);
}
