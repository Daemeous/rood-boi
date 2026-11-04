//! Whether a candidate needs its target engaged (D2: 73460.s; oracle pc/src/battle_movement.c).
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: func_800DBC60 (pc/src/battle_movement.c)
/// Candidate needs its target engaged: 1 when the state is busy (byte 0x13),
/// 0 without candidate option bit 4, else whether the target has no
/// engagement (D_800F45E0[target] null).
pub fn targetNeedsEngagement(state: [*]u8, candidate: [*]u8) callconv(.c) c_int {
    if (state[0x13] != 0) return 1;
    if (psx.byteAt(u32, candidate, 16).* & 0x10 == 0) return 0;
    const target: c_uint = psx.byteAt(u32, candidate, 12).* & 0xf;
    return @intFromBool(v.g.D_800F45E0()[target] == null);
}
