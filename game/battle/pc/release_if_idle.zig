//! Releasing a tile reservation of an idle actor (D2: 7DE94.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: func_800E6700 (pc/src/battle_movement.c)
/// Releases actor `id`'s reservation (vs_battle_aiReleaseActorAiReservations) when its command (byte
/// 9) is one of the set 0xA41800 and its model has no motion flags (0xA & 0x1F,
/// 0xB & 0xB).
pub fn releaseReservationIfIdle(id: c_int) callconv(.c) void {
    const actor: *ai.vs_battle_actor = @ptrCast(@alignCast(psx.at(v.g.vs_battle_actors(), id).* orelse return));
    const model = psx.at(v.g.vs_actors_actorRecords(), id).*;
    if (((@as(c_uint, 1) << @intCast(actor.unk9 & 31)) & 0xa41800) == 0) return;
    const body: [*]u8 = @ptrCast(model.?);
    if (((body[0xa] & 0x1f) | (body[0xb] & 0xb)) != 0) return;
    if (psx.at(v.g.vs_battle_aiActorAiStates(), id).*) |state| c.vs_battle_aiReleaseActorAiReservations(state);
}
