//! Blocked step test (D2: 7BDD4.s; oracle pc/src/battle_movement.c).
const c = @import("c_battle_movement");

/// orig: func_800E4690 (pc/src/battle_movement.c)
/// Whether stepping from `tile` in direction `dir` is blocked: the neighbour
/// has no floor (raw height negative) or differs by 64 or more.
pub fn stepBlocked(tile: c_uint, dir: c_int) callconv(.c) c_int {
    const x: c_int = @intCast(tile & 0xff); // a byte
    const z: c_int = @intCast((tile >> 16) & 0xff);
    const here_raw: c_uint = @bitCast(c.func_800E4320(x, z));
    const here: c_int = @as(c_int, @bitCast(here_raw << 17)) >> 17;
    const packed_dir: c_int = @as(*const i8, @ptrFromInt(@as(usize, @as(u32, @bitCast(@as(c_int, 0x1f800000) +% 0x3ec +% dir))))).*;
    const raw = c.func_800E4320(x +% directionDx(packed_dir), z +% directionDz(packed_dir));
    if (raw < 0) return 1;
    var difference: c_int = here -% (@as(c_int, @bitCast(@as(c_uint, @bitCast(raw)) << 17)) >> 17);
    if (difference < 0) difference = -%difference;
    return @intFromBool((difference >> 6) != 0);
}

/// orig: direction_dx (pc/src/battle_movement.c)
fn directionDx(packed_dir: c_int) c_int {
    return @as(c_int, @bitCast(@as(c_uint, @bitCast(packed_dir)) << 30)) >> 30;
}

/// orig: direction_dz (pc/src/battle_movement.c)
fn directionDz(packed_dir: c_int) c_int {
    return @as(c_int, @bitCast(@as(c_uint, @bitCast(packed_dir)) << 26)) >> 30;
}
