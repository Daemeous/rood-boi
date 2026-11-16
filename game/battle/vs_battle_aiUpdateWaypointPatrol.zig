//! Patrol waypoint walk: advances a patrolling actor's waypoint index and
//! sets its destination tile (pc/src/game/BATTLE/BATTLE.PRG/771A8.c).
const psx = @import("common").psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// orig: patrolWaypoint (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// The entry at `index` in `row`, unchecked (a file-scope static inline).
fn patrolWaypoint(row: [*]u16, index: c_int) c_uint {
    return @as(c_uint, psx.at(row, index).*);
}

/// orig: vs_battle_aiUpdateWaypointPatrol (pc/src/game/BATTLE/BATTLE.PRG/771A8.c)
/// Steps a patrol state to its next waypoint and sets the destination tile from it.
pub fn vs_battle_aiUpdateWaypointPatrol(state: ?*v.vs_battle_aiState) callconv(.c) void {
    const s = state.?;
    const table = v.g.D_800F58D8();
    const row: [*]u16 = &table.*.?[@as(usize, s.row)];
    const entry: c_uint = patrolWaypoint(row, @as(c_int, s.index));

    move: {
        if (@as(c_uint, psx.getBits(&s.tile.position.p, "x")) != (entry & 31) or
            @as(c_uint, psx.getBits(&s.tile.position.p, "z")) != ((entry >> 5) & 31))
        {
            if (s.timer104 != 0) s.timer104 -%= 1;
            // 1 << rate: the C shifts by rate & 31 (x86 shl masks the count), so mask it the same way.
            const shifted: c_int = @as(c_int, 1) << @as(u5, @truncate(s.rate102));
            if (c.vs_battle_aiNextActorRandomMasked(shifted) != 0 or s.timer104 != 0) break :move;
            s.direction = psx.conv(u8, -%@as(c_int, s.direction));
        }
        const step: c_int = @as(c_int, s.direction) +% 8;
        while (true) {
            s.index = psx.conv(u8, (@as(c_int, s.index) +% step) & 7);
            if ((patrolWaypoint(row, @as(c_int, s.index)) >> 15) != 0) break;
        }
        s.timer104 = 10;
    }

    psx.setBits(&s.destination168.movementPosition.p, "x", psx.conv(u8, patrolWaypoint(row, @as(c_int, s.index)) & 31));
    psx.setBits(&s.destination168.movementPosition.p, "z", psx.conv(u8, (patrolWaypoint(row, @as(c_int, s.index)) >> 5) & 31));
    c.vs_battle_aiDispatchAiLocomotionMode(@ptrCast(state), 0, 0);
}
