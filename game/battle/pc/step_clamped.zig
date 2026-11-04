//! Clamped step of a value (D2: 73B0C.s; oracle pc/src/battle_movement.c).
const psx = @import("common").psx;

/// orig: func_800DC30C (pc/src/battle_movement.c)
/// Moves `value` by `step` (up when `mode` is 2, else down) and clamps it to 0..limit.
pub fn stepClamped(value: *i16, limit: *const i16, step: c_int, mode: c_int) callconv(.c) void {
    const next: c_int = if (mode == 2) @as(c_int, value.*) +% step else @as(c_int, value.*) -% step;
    value.* = if (next < 0) 0 else if (@as(c_int, limit.*) < next) limit.* else psx.conv(i16, next);
}
