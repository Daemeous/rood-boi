//! Passes a sub-block of an entry in the vs_actors_actorRecords table on to vs_audio_playActorModelSound
//! (pc/src/game/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_44F14");
const c = @import("c_44F14");

/// orig: vs_audio_playActorModelSoundVariant (pc/src/game/BATTLE/BATTLE.PRG/44F14.c)
/// Calls vs_audio_playActorModelSound on the first block of table entry `arg0`, with `arg1 + 4`.
pub fn vs_audio_playActorModelSoundVariant(arg0: c_int, arg1: c_int) callconv(.c) void {
    // vs_actors_actorRecords[arg0]: the C does not check arg0 against the table's 17 entries.
    const entry = psx.at(v.g.vs_actors_actorRecords(), arg0).*.?;
    c.vs_audio_playActorModelSound(@ptrCast(&entry.unk0), arg1 +% 4);
}
