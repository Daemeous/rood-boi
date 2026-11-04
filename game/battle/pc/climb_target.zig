//! Climb target search (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E49D8 (pc/src/battle_movement.c)
/// Tries climbing toward a higher tile: from `tile` walks up to steps - 1
/// tiles in `dir` while the floor stays at least as high; on the first lower
/// tile returns it if func_800D96C8 and func_800D954C accept the step, else 0.
/// Only with the occupancy bitmap active (0x3F4). The original intended
/// min(steps - 1, 2) but tests its constant, so the bound is steps - 1.
pub fn climbTarget(state: [*]u8, tile_in: c_uint, dir: c_int, steps: c_int) callconv(.c) c_uint {
    var tile = tile_in;
    if (@as(*const u32, @ptrFromInt(0x1f8003f4)).* == 0) return 0;
    const current = @as(*align(1) const u32, @ptrCast(state + 0x34)).*;
    var floor = c.func_800E42E0(current);
    const raw = c.func_800E4308(tile);
    if (raw < 0) return 0;
    const target: c_int = @as(c_int, @bitCast(@as(c_uint, @bitCast(raw)) << 17)) >> 17;
    if (!(floor < target) or steps == 0) return 0;
    floor = target;
    const limit = steps -% 1;
    var taken: c_int = 0;
    const packed_dir: c_int = @as(*const u8, @ptrFromInt(@as(usize, @as(u32, @bitCast(@as(c_int, 0x1f800000) +% 0x3ec +% dir))))).*;
    var x: c_int = @intCast(tile & 0xff); // a byte
    var z: c_int = @intCast((tile >> 16) & 0xff);
    while (true) {
        x +%= directionDx(packed_dir);
        z +%= directionDz(packed_dir);
        const height = c.func_800E42EC(x, z);
        if (height < floor) break;
        taken +%= 1;
        if (!(taken < limit)) return 0;
    }
    tile = (tile & 0xff00ff00) | @as(c_uint, @bitCast(x)) | (@as(c_uint, @bitCast(z)) << 16);
    if (c.func_800D96C8(state, current, tile, dir) <= 0 or c.func_800D954C(state, tile) <= 0) return 0;
    return tile;
}

/// orig: direction_dx (pc/src/battle_movement.c)
fn directionDx(packed_dir: c_int) c_int {
    return @as(c_int, @bitCast(@as(c_uint, @bitCast(packed_dir)) << 30)) >> 30;
}

/// orig: direction_dz (pc/src/battle_movement.c)
fn directionDz(packed_dir: c_int) c_int {
    return @as(c_int, @bitCast(@as(c_uint, @bitCast(packed_dir)) << 26)) >> 30;
}
