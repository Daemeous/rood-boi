//! Frees the MPD section buffers of the room (src/BATTLE/BATTLE.PRG/4A0A8.c).
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BEB34 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs the stage-3 hook, then frees the cleared-section and room-door-section buffers if they are set.
pub fn func_800BEB34() callconv(.c) void {
    if (v.g.D_800F4BE2().* == 3) {
        c.func_800BFD9C();
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
