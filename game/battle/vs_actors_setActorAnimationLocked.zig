//! Sets the flag bit unk5AC_24 on the enemy actor in slot `index` of vs_actors_actorRecords (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");

/// orig: vs_actors_setActorAnimationLocked (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores `value`'s low bit in unk5AC_24 of the actor in slot `index`, if the slot is occupied.
pub fn vs_actors_setActorAnimationLocked(index: c_int, value: c_int) callconv(.c) void {
    // The C does not bounds-check `index` against the 17 slots: psx.at does not either.
    if (psx.at(v.g.vs_actors_actorRecords(), index).*) |actor| {
        psx.setBits(actor, "unk5AC_24", psx.conv(u1, value));
    }
}
