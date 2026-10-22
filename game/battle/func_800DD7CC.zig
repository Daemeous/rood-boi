//! Finds the nearest enabled waypoint to a state's position and writes its cell to a destination (src/BATTLE/BATTLE.PRG/74088.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// Reads waypoint slot `index` from the waypoint table, as the C re-reads it each time.
fn waypointAt(index: c_int) u16 {
    return v.g.D_800F58C8().*.?[@intCast(index)];
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
        const entry: u16 = waypointAt(index);
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
                dst.x = psx.conv(u8, @as(c_uint, waypointAt(index)) & 0x1F);
                closest = distance;
                dst.z = psx.conv(u8, (@as(c_uint, waypointAt(index)) >> 5) & 0x1F);
            }
        }
    }

    scratch.* = savedMap;
}
