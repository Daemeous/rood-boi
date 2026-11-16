//! Whether a candidate's target counts as engaged (D2: 72B70.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiCanApproachEngagedTarget (pc/src/battle_movement.c)
/// Whether a candidate's target counts as engaged in the room's current
/// encounter (vs_battle_aiRoomAiState): forced by 0x8E, blocked by 0x98, otherwise the
/// target relation bit 0 plus the room's counters and per-actor mask. The
/// original calls vs_battle_aiIsActionCooldownReady and discards its result.
pub fn canEngageTarget(state: *ai.vs_battle_aiState, candidate: [*]u8) callconv(.c) c_int {
    const room = v.g.vs_battle_aiRoomAiState().*.?;
    _ = c.vs_battle_aiIsActionCooldownReady(state, candidate);
    if (state.timer8E != 0) return 1;
    if (state.timer98 != 0) return 0;
    const target: usize = psx.byteAt(u32, candidate, 12).* & 0xf;
    if (psx.fieldAt(u32, &state.tbl.t.targets.bits32, @as(isize, @intCast(target * 4))).* & 1 == 0) return 0; // target < 16

    const count: c_uint = room.unkD;
    if (count < @as(c_uint, @bitCast(room.unk24))) return 0;
    // The actor id is a byte: the x86 shift takes the count modulo 32.
    const bit: c_uint = @as(c_uint, 1) << @truncate(@as(c_uint, state.id));
    if (@as(c_uint, room.unk10) & bit != 0) return 1;
    return @intFromBool(!(count < room.unk12));
}
