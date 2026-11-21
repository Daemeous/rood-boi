//! Steps a movement position one tile toward a direction, or toward the nearer of two (pc/src/game/BATTLE/BATTLE.PRG/6FC00.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_6FC00");
const c = @import("c_6FC00");

/// orig: vs_battle_aiFindAdjacentMovementDestination (pc/src/game/BATTLE/BATTLE.PRG/6FC00.c)
/// Tries a step from the state's position toward a direction (or the nearer of two), returning the reached position's raw bits.
pub fn vs_battle_aiFindAdjacentMovementDestination(state: ?*v.vs_battle_aiState, first: c_int, second: c_int, dir_in: c_uint) callconv(.c) c_uint {
    if (state.?.flags4.b[3] != 0) return 0;

    // dest.p.y is never written by the original (uninitialised there); 0 here.
    var dest: v.vs_battle_movementPosition = .{ .raw = 0 };
    psx.setBits(&dest.p, "status", 1);
    const source = state.?.tile.position;

    var dir: c_uint = dir_in;
    var directions: c_uint = dir_in;
    var count: c_int = 1;
    if (first != second) {
        const quadrant: c_uint = (dir_in & 0xFFF) >> 10;
        dir = if ((dir_in & 0x3FF) > 512) ((quadrant +% 1) & 3) *% 2 else quadrant *% 2;
        if (dir != psx.conv(c_uint, first)) {
            directions = (psx.conv(c_uint, first) << 16) | dir;
        } else {
            directions = (psx.conv(c_uint, second) << 16) | dir;
        }
        count = 2;
    }

    const table: [*]align(1) v.D_800F16EC_t = pad.directions(v.D_800F16EC_t);

    const Exit = enum { none, blocked, done };
    const exit: Exit = blk: {
        var i: c_int = 0;
        while (i < count) : (i += 1) {
            // i is 0 or 1 here, so the shift amount is 0 or 16.
            dir = (directions >> @as(u5, @intCast(i *% 16))) & 0xFFFF;
            const entry = &table[@as(usize, dir)];
            psx.setBits(&dest.p, "x", psx.conv(u8, @as(c_int, psx.getBits(&source.p, "x")) +% @as(c_int, psx.getBits(entry, "dx"))));
            psx.setBits(&dest.p, "z", psx.conv(u8, @as(c_int, psx.getBits(&source.p, "z")) +% @as(c_int, psx.getBits(entry, "dz"))));

            var result = c.vs_battle_aiCanTraverseTileEdge(@ptrCast(state), source.raw, dest.raw, psx.conv(c_int, dir));
            if (result > 0) {
                result = c.vs_battle_aiDestinationOccupancy(@ptrCast(state), dest.raw);
                if (result < 0) break :blk .blocked;
                if (result != 0) break :blk .done;
                return 0;
            }
            if (result < 0) break :blk .blocked;

            dest.raw = c.vs_battle_aiFindGapLandingTile(@ptrCast(state), dest.raw, psx.conv(c_int, dir), source.raw);
            const status = psx.getBits(&dest.p, "status");
            if (status == 1) break :blk .done;
            if (status != 0) break :blk .blocked;
        }
        break :blk .none;
    };

    switch (exit) {
        .none => return 0,
        .blocked => psx.setBits(&dest.p, "status", 2),
        .done => {},
    }
    return dest.raw & 0xFFFF00FF;
}
