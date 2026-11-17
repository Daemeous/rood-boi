//! Whether an actor may step between two tiles (D2: 70D4C.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;
const pad = @import("common").pad;

fn scratch8(offset: c_int) *u8 {
    return @ptrFromInt(pad.addr(@intCast(offset)));
}

/// orig: nav_cell (pc/src/battle_movement.c)
/// The navigation cell of tile (x, z) in the grid cached at scratchpad 0x3C0.
fn navCell(x: c_int, z: c_int) *align(1) u16 {
    const cells: usize = @intFromPtr(pad.nav.cells.?);
    return @ptrFromInt(cells +% @as(usize, @bitCast(@as(isize, z *% 64 +% x *% 2))));
}

/// orig: vs_battle_aiCanTraverseTileEdge (pc/src/battle_movement.c)
/// Whether the actor may step from tile `from` to tile `to` in direction
/// `dir`: 1 yes, 0 no, -1 when the step leaves its approach radius. Quirks
/// kept: for a negative X step the original negates the Z delta instead of
/// X; the area-code test always uses byte 0x132 (0x154 is loaded then
/// overwritten); the approach test reads halfwords from the vs_actors_actorRecords
/// array itself (+0x1C/+0x20 past the entry, not through it) and passes the
/// packed `to` tile as the SQR Z input.
pub fn stepAllowed(state: *ai.vs_battle_aiState, from: c_uint, to: c_uint, dir: c_int) callconv(.c) c_int {
    if (state.flags4.b[3] != 0) return 0;
    const x: c_int = @intCast(to & 0xff);
    const z: c_int = @intCast((to >> 16) & 0xff);
    if (!(x < scratch8(0x3dc).* and z < scratch8(0x3dd).*)) return 0;
    const cell: c_uint = navCell(x, z).*;
    if (cell & 0x30 != 0) return 0;
    if (state.flags0.b[2] == 0 and (cell & 0xf) != state.terrain132) return 0;
    const from_x: c_int = @intCast(from & 0xff);
    const from_z: c_int = @intCast((from >> 16) & 0xff);
    const step_x: c_int = x -% from_x;
    var step_z: c_int = z -% from_z;
    // Evaluated left to right in the object.
    const rise: c_int = c.vs_field_groundHeightAtTile(x, z) -% c.vs_field_groundHeightAtTile(from_x, from_z);
    if (step_x < 0) step_z = -%step_z;
    if (step_z < 0) step_z = -%step_z;
    const climbing: c_int = state.flags0.b[3];
    var checked = false;
    if (step_x < 2 and step_z < 2) {
        if (c.vs_battle_aiDirectionalBlockingActor(state, dir) != 0) return 0;
        checked = (climbing | @intFromBool(rise < 1)) == 0;
    }
    if (!checked) {
        if (@as(c_int, state.radiusB2) < step_x +% step_z) return 0;
        const limit: c_int = if (state.flags & 0xf8 != 0) 0x2000 else state.fB0;
        if ((@intFromBool(limit < rise) & climbing) != 0) return 0;
        if (rise < -%limit) return 0;
    }
    if (state.flags4.b[1] == 0) return 1;
    if (state.unk89 == 1) {
        // The array's own address plus id * 4 (not * 8 on 64-bit): kept.
        const entry: usize = @intFromPtr(v.g.vs_actors_actorRecords()) +% @as(usize, state.actorId) *% 4;
        const radius: c_int = state.range -% 0x60;
        const distance = c.vs_core_mathSquaredVectorLength(
            x *% 128 +% 64 -% @as(c_int, psx.byteAt(u16, @as([*]u8, @ptrFromInt(entry)), 0x1c).*),
            z *% 128 +% 64 -% @as(c_int, psx.byteAt(u16, @as([*]u8, @ptrFromInt(entry)), 0x20).*),
            @bitCast(to),
        );
        if (distance < @as(c_int, @bitCast(@as(c_uint, @bitCast(radius)) *% @as(c_uint, @bitCast(radius))))) return -1;
    }
    const target = state.destination168.destination;
    const current = c.vs_core_mathSquaredTileDistance(state.tile.raw, target);
    const next = c.vs_core_mathSquaredVectorLength(
        x -% @as(c_int, @intCast(target & 0xff)),
        0,
        z -% @as(c_int, @intCast((target >> 16) & 0xff)),
    );
    return @intFromBool(next < current);
}
