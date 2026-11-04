//! Passability of the four axis directions (D2: 7B55C.s; oracle pc/src/battle_movement.c).

/// orig: func_800E3D5C (pc/src/battle_movement.c)
/// Passability bits of the four axis directions (0, 2, 4, 6) as bits 3..0.
/// The original stashes its state in GTE registers (IR0-IR3, SXY0/1, MAC1);
/// the oracle does not reproduce that, nor does this.
pub fn axisPassability(state: ?*anyopaque, @"test": ?*const fn (?*anyopaque, c_int) callconv(.c) c_int) callconv(.c) c_int {
    const a = @"test".?(state, 0);
    const b = @"test".?(state, 2);
    const c = @"test".?(state, 4);
    const d = @"test".?(state, 6);
    return (a << 3) | (b << 2) | (c << 1) | d;
}
