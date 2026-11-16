//! Joining the room's encounter (D2: 6FA6C.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const v = @import("views_battle_movement");
const psx = @import("common").psx;

/// orig: vs_battle_aiTryEngageActor (pc/src/battle_movement.c)
/// Joins the room's encounter. Already engaged (0x8E) succeeds; a full
/// encounter (count 0x0D below members 0x12) or a block (0x98 or D_800F58E0)
/// fails. Otherwise sets the engagement timer (1, or 0x20 after a recent
/// leave at 0x9A), adds the actor's bit to the mask at 0x10, bumps the
/// members and pushes the actor on the room's engaged list (head 0x1C, tail
/// 0x20, link 0x1A0).
pub fn joinEncounter(state: *ai.vs_battle_aiState) callconv(.c) c_int {
    const room = v.g.vs_battle_aiRoomAiState().*.?;
    if (state.timer8E != 0) return 1;
    const members: c_uint = room.unk12;
    if (room.unkD < members) return 0;
    if ((state.timer98 | v.g.D_800F58E0().*) != 0) return 0;
    state.unk12C = 0;
    // The actor id is a byte: the x86 shift takes the count modulo 32.
    const bit: c_uint = @as(c_uint, 1) << @truncate(@as(c_uint, state.id));
    room.unk10 = psx.conv(u16, @as(c_uint, room.unk10) | bit);
    room.unk12 = psx.conv(u8, members +% 1);
    state.timer8E = if (state.timer != 0) 0x20 else 1;
    const head = room.unk1C;
    if (head == null) room.unk20 = @ptrCast(state);
    state.next1A0 = @ptrCast(head);
    room.unk1C = @ptrCast(state);
    return 1;
}
