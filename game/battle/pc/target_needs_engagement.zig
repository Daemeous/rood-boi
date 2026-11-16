//! Whether a candidate needs its target engaged (D2: 73460.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiDoesActionRequireCooldown (pc/src/battle_movement.c)
/// Candidate needs its target engaged: 1 when the state is busy (byte 0x13),
/// 0 without candidate option bit 4, else whether the target has no
/// engagement (vs_field_gimmickInstances[target] null).
pub fn targetNeedsEngagement(state: *ai.vs_battle_aiState, candidate: [*]u8) callconv(.c) c_int {
    if (state.flag13 != 0) return 1;
    if (psx.byteAt(u32, candidate, 16).* & 0x10 == 0) return 0;
    const target: c_uint = psx.byteAt(u32, candidate, 12).* & 0xf;
    return @intFromBool(v.g.vs_field_gimmickInstances()[target] == null);
}
