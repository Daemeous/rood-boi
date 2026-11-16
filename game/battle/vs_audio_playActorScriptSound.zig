//! Forwards battle actor slot's unk44 record to vs_audio_playActorModelSound (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_audio_playActorScriptSound (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_audio_playActorModelSound with the unk44 record of battle actor `slot`, for slots below 16 that are set.
pub fn vs_audio_playActorScriptSound(slot: c_int, arg1: c_int) callconv(.c) void {
    // The C reads vs_battle_actors[slot] before testing slot < 16: no bounds check.
    const actor = psx.at(v.g.vs_battle_actors(), slot).*;
    if (actor != null and slot < 16) {
        c.vs_audio_playActorModelSound(@ptrCast(psx.get(actor.?.unk44)), arg1);
    }
}
