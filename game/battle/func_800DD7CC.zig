//! Finds the nearest enabled waypoint to a state's position and writes its cell to a destination (src/BATTLE/BATTLE.PRG/74088.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// orig: nearestWaypointAddress (src/BATTLE/BATTLE.PRG/74088.c)
/// `row + index`, unchecked (a file-scope static inline).
fn nearestWaypointAddress(row: [*]u16, index: c_int) *u16 {
    return psx.at(row, index);
}

/// orig: nearestWaypointValue (src/BATTLE/BATTLE.PRG/74088.c)
/// `row[index]`, unchecked (a file-scope static inline).
fn nearestWaypointValue(row: [*]u16, index: c_int) c_uint {
    return psx.at(row, index).*;
}

/// orig: func_800DD7CC (src/BATTLE/BATTLE.PRG/74088.c)
/// Scans the eight waypoint slots and writes the cell of the nearest enabled one to destination.
pub fn func_800DD7CC(state: ?*v.nearestWaypointState, destination: ?*v.nearestWaypointDestination) callconv(.c) void {
    var closest: c_uint = 0xFFFF_FFFF;
    const scratch: *c_int = @ptrFromInt(0x1F8003BC);
    var index: c_int = 0;
    const savedMap: c_int = scratch.*;
    scratch.* = @as(c_int, @bitCast(psx.addr(v.g.D_800F58D0().*)));

    while (index < 8) : (index +%= 1) {
        const entry: u16 = nearestWaypointAddress(v.g.D_800F58C8().*.?, index).*;
        const @"packed": c_uint = @as(c_uint, entry) & 0xFFFF;
        if ((@"packed" >> 15) != 0) {
            const x: c_int = ((@as(c_int, entry) & 0x1F) << 7) +% 0x40;
            const z: c_int = @bitCast(((@"packed" *% 4) & 0xF80) +% 0x40);
            const st = state.?;
            const dx: c_int = @as(c_int, st.x) -% x;
            const dz: c_int = @as(c_int, st.z) -% z;
            const groundY: c_int = c.func_800E42D4(x, z);
            const dy: c_int = @as(c_int, st.y) -% groundY;
            const distance: c_uint = c.func_800E4660(dx, dy, dz);
            if (distance < closest) {
                const dst = destination.?;
                dst.x = psx.conv(u8, nearestWaypointValue(v.g.D_800F58C8().*.?, index) & 0x1F);
                closest = distance;
                dst.z = psx.conv(u8, (nearestWaypointValue(v.g.D_800F58C8().*.?, index) >> 5) & 0x1F);
            }
        }
    }

    scratch.* = savedMap;
}
