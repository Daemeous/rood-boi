//! Starting a walk to a tile (D2: 72B70.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: func_800DB5DC (pc/src/battle_movement.c)
/// Enters movement mode 2 to walk to `tile` (stored at 0x168): within
/// `range` of the tile centre (L800E5030 in 7C7E0.s; height from
/// func_800E42E0, squared distances compared unsigned) the walk finishes
/// (func_800DEEFC 0), otherwise it moves (func_800E50A0 0, heading 0).
pub fn startWalkToTile(state: [*]u8, tile_in: c_uint, range: c_int) callconv(.c) void {
    psx.byteAt(u32, state, 0x168).* = tile_in;
    _ = c.func_800DB4AC(state, 2);
    const tile = psx.byteAt(u32, state, 0x168).*;
    const y = c.func_800E42E0(tile);
    const distance: c_uint = @bitCast(c.func_800E4660(
        @as(c_int, @bitCast((tile & 0xff) << 7)) +% 64 -% psx.byteAt(i16, state, 0x4c).*,
        y -% psx.byteAt(i16, state, 0x4e).*,
        @as(c_int, @bitCast(((tile >> 16) & 0xff) << 7)) +% 64 -% psx.byteAt(i16, state, 0x50).*,
    ));
    if (distance < @as(c_uint, @bitCast(range *% range))) {
        c.func_800DEEFC(state, 0);
    } else {
        c.func_800E50A0(state, 0, 0);
    }
}
