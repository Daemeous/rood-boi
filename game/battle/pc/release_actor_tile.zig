//! Releasing an actor's navigation tile (D2: 7DE94.s; oracle pc/src/actor_cleanup.c).
const c = @import("c_actor_cleanup");
const v = @import("views_actor_cleanup");

/// orig: func_800E678C (pc/src/actor_cleanup.c)
/// Clears the actor state's byte 0x1A, clears the cell flag of its tile
/// (bytes 0x170 and 0x172) when that lies inside the grid record's bounds
/// (bytes 1, 0, 3, 2), and for a state with byte 0x19B set masks grid byte
/// 0xE with the bits from its own low five bits up.
pub fn releaseActorTile(actor: [*]u8) callconv(.c) void {
    const grid: [*]u8 = @ptrFromInt(@as(usize, v.g.D_800F58BC().*));
    const x: c_uint = actor[0x170];
    const z: c_uint = actor[0x172];
    const active = actor[0x19b];
    actor[0x1a] = 0;
    if (x >= grid[1] and x <= grid[0] and z >= grid[3] and z <= grid[2]) c.func_800E4C28(@bitCast(x), @bitCast(z));
    if (active != 0) {
        const value: c_uint = grid[0xe];
        // The original uses the byte itself as a masked variable shift.
        grid[0xe] = @truncate(value & (0 -% (@as(c_uint, 1) << @intCast(value & 31))));
    }
}
