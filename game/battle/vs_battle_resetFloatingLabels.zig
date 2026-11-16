//! Resets the battle status-text queue (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const v = @import("views_2842C");
const c = @import("c_2842C");

/// sizeof *vs_battle_floatingLabels: the queue is D_800F1BAC_t[32] (the views give its pointer opaque).
const queue_size = 32 * @sizeOf(v.D_800F1BAC_t);

/// orig: vs_battle_resetFloatingLabels (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Clears the queue's counters and flags and zeroes its 32 entries.
pub fn vs_battle_resetFloatingLabels() callconv(.c) void {
    v.g.vs_battle_actorMarkersHidden().* = 0;
    v.g.vs_battle_abilityTimingCueFrames().* = 0;
    v.g.vs_battle_floatingLabelCount().* = 0;
    v.g.D_800F2278().* = 0;
    v.g.D_800F227A().* = 0;
    c.vs_main_bzero(v.g.vs_battle_floatingLabels().*, queue_size);
}
