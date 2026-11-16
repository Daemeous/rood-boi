//! Frees the MPD section buffers of the room (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_freeRoomEventSections (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs the stage-3 hook, then frees the cleared-section and room-door-section buffers if they are set.
pub fn vs_event_freeRoomEventSections() callconv(.c) void {
    if (v.g.vs_event_roomChangeState().* == 3) {
        c.vs_event_releaseScriptActorControl();
    }
    const cleared = v.g._mpdClearedSection().*;
    if (cleared != null) {
        c.vs_main_freeHeap(cleared);
    }
    const doorSection = v.g._mpdRoomDoorSection().*;
    if (doorSection != null) {
        c.vs_main_freeHeap(doorSection);
    }
}
