//! Patrol waypoint walk: advances a patrolling actor's waypoint index and
//! sets its destination tile (src/BATTLE/BATTLE.PRG/771A8.c).
const psx = @import("common").psx;
const v = @import("views_771A8");
const c = @import("c_771A8");

/// C's patrolWaypoint (a file-scope static): the entry at `index` in `row`, unchecked.
fn patrolWaypoint(row: [*]u16, index: c_int) c_uint {
    return @as(c_uint, psx.at(row, index).*);
}

/// orig: func_800E23AC (src/BATTLE/BATTLE.PRG/771A8.c)
/// Steps a patrol state to its next waypoint and sets the destination tile from it.
pub fn func_800E23AC(state: ?*v.patrolWaypointState) callconv(.c) void {
    const s = state.?;
    const table = v.g.D_800F58D8();
    const row: [*]u16 = &table.*.?[@as(usize, s.row)];
    const entry: c_uint = patrolWaypoint(row, @as(c_int, s.index));

    move: {
        if (@as(c_uint, psx.getBits(&s.tile.p, "x")) != (entry & 31) or
            @as(c_uint, psx.getBits(&s.tile.p, "z")) != ((entry >> 5) & 31))
        {
            if (s.timer != 0) s.timer -%= 1;
            // 1 << rate: the C shifts by rate & 31 (x86 shl masks the count), so mask it the same way.
            const shifted: c_int = @as(c_int, 1) << @as(u5, @truncate(s.rate));
            if (c.func_800E45D4(shifted) != 0 or s.timer != 0) break :move;
            s.direction = psx.conv(u8, -%@as(c_int, s.direction));
        }
        const step: c_int = @as(c_int, s.direction) +% 8;
        while (true) {
            s.index = psx.conv(u8, (@as(c_int, s.index) +% step) & 7);
            if ((patrolWaypoint(row, @as(c_int, s.index)) >> 15) != 0) break;
        }
        s.timer = 10;
    }

    psx.setBits(&s.dest.p, "x", psx.conv(u8, patrolWaypoint(row, @as(c_int, s.index)) & 31));
    psx.setBits(&s.dest.p, "z", psx.conv(u8, (patrolWaypoint(row, @as(c_int, s.index)) >> 5) & 31));
    c.func_800E50A0(@ptrCast(state), 0, 0);
}
