//! Releasing an actor's navigation tile (D2: 7DE94.s; oracle pc/src/actor_cleanup.c).
const ai = @import("views_71618");
const c = @import("c_actor_cleanup");
const v = @import("views_actor_cleanup");
const psx = @import("common").psx;

/// orig: vs_battle_aiReleaseActorAiReservations (pc/src/actor_cleanup.c)
/// Clears the actor state's byte 0x1A, clears the cell flag of its tile
/// (bytes 0x170 and 0x172) when that lies inside the grid record's bounds
/// (bytes 1, 0, 3, 2), and for a state with byte 0x19B set masks grid byte
/// 0xE with the bits from its own low five bits up.
pub fn releaseActorTile(state: *ai.vs_battle_aiState) callconv(.c) void {
    const grid = v.g.vs_battle_aiRoomAiState().*.?;
    const x: c_uint = psx.fieldAt(u8, &state.next, 0).*;
    const z: c_uint = psx.fieldAt(u8, &state.next, 2).*;
    const active = state.gate;
    state.f1A = 0;
    // As the assembly reads the bounds: x between bytes 1 and 0, z between bytes 3 and 2 (the record's names are the C's).
    if (x >= grid.maxZ and x <= grid.maxX and z >= grid.minZ and z <= grid.minX) c.vs_battle_aiReleaseAiDestinationTile(@bitCast(x), @bitCast(z));
    if (active != 0) {
        const value: c_uint = grid.closedGates;
        // The original uses the byte itself as a masked variable shift.
        grid.closedGates = @truncate(value & (0 -% (@as(c_uint, 1) << @intCast(value & 31))));
    }
}
