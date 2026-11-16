//! Leaving the room's encounter (D2: 6FA6C.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiDisengageActor (pc/src/battle_movement.c)
/// Leaves the encounter: clears the actor's bit in the room's mask at 0x10
/// and its timer at 0x8E, blocks re-joining (0x98 = 0x30 unless exactly two
/// actors remain; 0x9A = 90), unlinks the actor (vs_battle_aiUnlinkEngagedActor), drops a
/// member and notifies actor `id` (vs_battle_aiSetActorAwarenessIndicator). Does nothing when the
/// actor's bit is not set.
pub fn leaveEncounter(state: *ai.vs_battle_aiState) callconv(.c) void {
    const room = v.g.vs_battle_aiRoomAiState().*.?;
    const id: c_int = state.id;
    const mask: c_uint = room.unk10;
    // id >= 32 is undefined in C; the x86 shift takes the count modulo 32.
    const bit: c_uint = @as(c_uint, 1) << @truncate(@as(c_uint, @bitCast(id)));
    if (mask & bit == 0) return;
    room.unk10 = psx.conv(u16, mask -% bit);
    state.timer8E = 0;
    const first: *ai.vs_battle_actor = @ptrCast(@alignCast(v.g.vs_battle_actors()[0].?));
    const second = first.next;
    state.timer98 = if (second != null and second.?.next == null) 0 else 0x30;
    state.timer = 90;
    c.vs_battle_aiUnlinkEngagedActor(state);
    room.unk12 -%= 1;
    c.vs_battle_aiSetActorAwarenessIndicator(id, 0);
}
