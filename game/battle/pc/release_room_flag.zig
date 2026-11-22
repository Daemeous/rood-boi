//! Releasing an actor's room flag (D2: 7E05C.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: vs_battle_aiReleaseActorAiContactOccupancy (pc/src/battle_movement.c)
/// When actor `id` is of type 11 or 14, its room-flag word (0x170, bits 8-15)
/// is set and its model byte 0xC has a zero low nibble, clears that room
/// flag. A missing record reads PS1 low RAM, as the original does.
pub fn releaseActorRoomFlag(id: c_int) callconv(.c) void {
    const actor = psx.at(v.g.vs_battle_actors(), id).*;
    const state_slot = psx.at(v.g.vs_battle_aiActorAiStates(), id).*;
    if (actor == null and state_slot == null) return;
    const low_ram: [*]u8 = @ptrCast(psx.nullObject(u8));
    const state: *ai.vs_battle_aiState = if (state_slot) |s| @ptrCast(@alignCast(s)) else @ptrCast(@alignCast(low_ram));
    const kind: c_uint = if (actor) |a| @as(*ai.vs_battle_actor, @ptrCast(@alignCast(a))).unk9 else low_ram[9];
    if (((@as(c_uint, 1) << @intCast(kind & 31)) & 0x4800) == 0) return;
    const flags: c_uint = state.next.movementPosition.raw & 0xff00;
    const model: [*]u8 = @ptrCast(state.object.actorObject);
    if (flags == 0 or (model[0xc] & 0xf) != 0) return;
    c.vs_battle_aiClearAiContactOccupancy(@bitCast(flags >> 8));
}
