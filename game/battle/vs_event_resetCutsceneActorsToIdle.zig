//! Battle slot pass: calls `vs_actors_playActorAnimation` for each of slots 0..16 that `vs_actors_isBattleActorSlot` reports (pc/src/game/BATTLE/BATTLE.PRG/573B8.c).
const c = @import("c_573B8");

/// orig: vs_event_resetCutsceneActorsToIdle (pc/src/game/BATTLE/BATTLE.PRG/573B8.c)
/// For each slot 0..16 that `vs_actors_isBattleActorSlot` returns an actor for, calls `vs_actors_playActorAnimation(slot, 1, 0, 0)`.
pub fn vs_event_resetCutsceneActorsToIdle() callconv(.c) void {
    var i: i16 = 0;
    while (i < 17) : (i +%= 1) {
        if (c.vs_actors_isBattleActorSlot(@as(c_int, i)) != null) {
            c.vs_actors_playActorAnimation(@as(c_int, i), 1, 0, 0);
        }
    }
}
