//! Resolving a step onto a flagged cell (D2: 72B70.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");
const psx = @import("common").psx;

fn scratch8(offset: c_int) *u8 {
    return @ptrFromInt(@as(u32, @bitCast(0x1f800000 +% offset)));
}

/// orig: direction_dx (pc/src/battle_movement.c)
/// The signed X step in bits 0-1 of a packed direction.
fn directionDx(packed_direction: c_int) c_int {
    return @as(c_int, @bitCast(@as(c_uint, @bitCast(packed_direction)) << 30)) >> 30;
}

/// orig: direction_dz (pc/src/battle_movement.c)
/// The signed Z step in bits 4-5 of a packed direction.
fn directionDz(packed_direction: c_int) c_int {
    return @as(c_int, @bitCast(@as(c_uint, @bitCast(packed_direction)) << 26)) >> 30;
}

/// orig: func_800DB93C (pc/src/battle_movement.c)
/// Resolves a step onto a cell flagged 0x20 (e.g. a ledge or gap): keeps
/// stepping in `dir` while cells stay flagged; at the first clear cell
/// within min(0xB2, 3) steps, returns it with the step/entry result
/// (func_800D96C8, then func_800D954C) in bits 24-31. Unflagged targets are
/// returned unchanged. As in the original, the flagged-cell walk continues
/// past the step limit while byte 0xB2 is nonzero.
pub fn resolveFlaggedCell(state: [*]u8, tile_in: c_uint, dir: c_int, from: c_uint) callconv(.c) c_uint {
    const cells: usize = psx.byteAt(u32, scratch8(0x3c0), 0).*;
    var tile = tile_in & 0xffffff;
    var x: c_int = @intCast(tile & 0xff);
    var z: c_int = @intCast(tile >> 16);
    const limit: c_int = state[0xb2];
    if (psx.byteAt(u16, @as([*]u8, @ptrFromInt(cells +% @as(usize, @intCast(z * 64 + x * 2)))), 0).* & 0x20 == 0) return tile; // 0 <= x, z <= 255
    const packed_direction: c_int = scratch8(0x3ec +% dir).*;
    var result: c_int = 0;
    const max_steps: c_int = if (limit < 3) limit else 3;
    var steps: c_int = 1;
    while (true) : (steps += 1) { // bounded by the 256-cell wrap of the walk
        x = (x +% directionDx(packed_direction)) & 0xff;
        z = (z +% directionDz(packed_direction)) & 0xff;
        tile = (@as(c_uint, @intCast(z)) << 16) | @as(c_uint, @intCast(x));
        if (psx.byteAt(u16, @as([*]u8, @ptrFromInt(cells +% @as(usize, @intCast(z * 64 + x * 2)))), 0).* & 0x20 != 0) {
            if (limit != 0) continue;
            break;
        }
        if (steps < max_steps) {
            result = c.func_800D96C8(state, from, tile, dir);
            if (result == 1) result = c.func_800D954C(state, tile);
        }
        break;
    }
    return tile | (@as(c_uint, @bitCast(result)) << 24);
}
