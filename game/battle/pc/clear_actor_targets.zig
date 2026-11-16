//! Dropping targets on an actor (D2: 7E05C.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;
const v = @import("views_battle_movement");

/// orig: vs_battle_aiClearActorAiLinksAndPlan (pc/src/battle_movement.c)
/// Walks the actor list from vs_battle_actors[0]: every actor whose 0x16
/// target is `id` drops it (0x10); then releases actor `id`'s reservation
/// through vs_battle_aiDisengageActor.
pub fn clearActorTargets(id: c_int) callconv(.c) void {
    var node: *ai.vs_battle_actor = @ptrCast(@alignCast(v.g.vs_battle_actors()[0].?));
    while (true) {
        const state: *ai.vs_battle_aiState = @ptrCast(@alignCast(psx.at(v.g.vs_battle_aiActorAiStates(), @as(c_uint, @bitCast(node.id))).*.?));
        if (@as(c_uint, @as(u8, @bitCast(state.target16))) == @as(c_uint, @bitCast(id))) state.target16 = 0x10;
        node = @ptrCast(@alignCast(psx.get(node.next) orelse break));
    }
    c.vs_battle_aiDisengageActor(psx.at(v.g.vs_battle_aiActorAiStates(), id).*);
}
