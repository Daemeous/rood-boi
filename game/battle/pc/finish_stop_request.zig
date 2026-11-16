//! Finishing a pending stop request (D2: 72B70.s; oracle pc/src/battle_movement.c).
const ai = @import("views_71618");
const c = @import("c_battle_movement");
const psx = @import("common").psx;

/// orig: func_800DB7B4 (pc/src/battle_movement.c)
/// Finishes a pending stop request (0x12D): request 1 with a basic command
/// (< 9) idles (command 1); otherwise releases the tile reservation
/// (vs_battle_aiReleaseActorAiReservations), clears the request and switches to command 2.
pub fn finishStopRequest(state: *ai.vs_battle_aiState) callconv(.c) void {
    const request: c_int = state.mode12D;
    if (request == 0) return;
    if (request == 1) {
        if (psx.get(state.actor).?.unk9 < 9) {
            c.vs_battle_aiIssueActorCommand(state, 1);
            return;
        }
    }
    c.vs_battle_aiReleaseActorAiReservations(state);
    state.mode12D = 0;
    c.vs_battle_aiIssueActorCommand(state, 2);
}
