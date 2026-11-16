//! Resetting an actor's forwarded values (D2: 6FA0C.s; oracle pc/src/actor_cleanup.c).
const ai = @import("views_71618");
const c = @import("c_actor_cleanup");

/// orig: vs_battle_aiResetActorAwarenessIndicator (pc/src/actor_cleanup.c)
/// Sends the actor's id (byte 0x88; none when 0) the values 0 and 0x40.
pub fn resetActorValues(state: *ai.vs_battle_aiState) callconv(.c) void {
    const id: c_uint = state.id;
    if (id != 0) {
        c.vs_battle_setActorMarker(@bitCast(id), 0);
        c.vs_battle_setActorMarker(@bitCast(id), 0x40);
    }
}
