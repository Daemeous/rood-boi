//! Runs vs_audio_playActorSoundPair on the actor in an enemy slot, with the per-animation table entry plus an offset (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_actors_playActorWeaponDrawSound (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Calls vs_audio_playActorSoundPair for the actor in `slot`, passing its table entry plus `offset`, when the slot is occupied.
pub fn vs_actors_playActorWeaponDrawSound(slot: c_int, offset: c_int) callconv(.c) void {
    // slot is not bounded by the C: psx.at indexes the pointer array as C does.
    const temp = psx.at(v.g.vs_actors_actorRecords(), slot).*;
    if (temp) |actor| {
        const table_entry = v.g.D_800E9090()[actor.unk5B1];
        c.vs_audio_playActorSoundPair(@ptrCast(actor), @as(c_int, table_entry) +% offset, 0);
    }
}
