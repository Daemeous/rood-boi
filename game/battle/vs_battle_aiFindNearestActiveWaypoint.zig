//! Finds the nearest enabled waypoint to a state's position and writes its cell to a destination (pc/src/game/BATTLE/BATTLE.PRG/74088.c).
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// orig: nearestWaypointAddress (pc/src/game/BATTLE/BATTLE.PRG/74088.c)
/// `row + index`, unchecked (a file-scope static inline).
fn nearestWaypointAddress(row: [*]u16, index: c_int) *u16 {
    return psx.at(row, index);
}

/// orig: nearestWaypointValue (pc/src/game/BATTLE/BATTLE.PRG/74088.c)
/// `row[index]`, unchecked (a file-scope static inline).
fn nearestWaypointValue(row: [*]u16, index: c_int) c_uint {
    return psx.at(row, index).*;
}

/// orig: vs_battle_aiFindNearestActiveWaypoint (pc/src/game/BATTLE/BATTLE.PRG/74088.c)
/// Scans the eight waypoint slots and writes the cell of the nearest enabled one to destination.
pub fn vs_battle_aiFindNearestActiveWaypoint(state: ?*v.vs_battle_aiState, destination: ?*v.nearestWaypointDestination) callconv(.c) void {
    var closest: c_uint = 0xFFFF_FFFF;
    var index: c_int = 0;
    const savedMap = pad.nav.floors; // the slot is borrowed for the special-terrain heights while the loop runs
    pad.nav.floors = v.g.D_800F58D0().*;

    while (index < 8) : (index +%= 1) {
        const entry: u16 = nearestWaypointAddress(v.g.D_800F58C8().*.?, index).*;
        const @"packed": c_uint = @as(c_uint, entry) & 0xFFFF;
        if ((@"packed" >> 15) != 0) {
            const x: c_int = ((@as(c_int, entry) & 0x1F) << 7) +% 0x40;
            const z: c_int = @bitCast(((@"packed" *% 4) & 0xF80) +% 0x40);
            const st = state.?;
            const dx: c_int = @as(c_int, st.position.v.vx) -% x;
            const dz: c_int = @as(c_int, st.position.v.vz) -% z;
            const groundY: c_int = c.vs_field_groundHeightAtWorldPosition(x, z);
            const dy: c_int = @as(c_int, st.position.v.vy) -% groundY;
            const distance: c_uint = c.vs_core_mathSquaredVectorLength(dx, dy, dz);
            if (distance < closest) {
                const dst = destination.?;
                dst.x = psx.conv(u8, nearestWaypointValue(v.g.D_800F58C8().*.?, index) & 0x1F);
                closest = distance;
                dst.z = psx.conv(u8, (nearestWaypointValue(v.g.D_800F58C8().*.?, index) >> 5) & 0x1F);
            }
        }
    }

    pad.nav.floors = savedMap;
}
