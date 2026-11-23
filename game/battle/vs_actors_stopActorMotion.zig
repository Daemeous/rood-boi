//! Clears the state of one entry of the vs_actors_actorRecords table (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_actors_stopActorMotion (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Releases the vs_actors_actorRecords entry `arg0` if present: calls vs_actors_clearActorMovementModes, then clears its flags and timer words.
pub fn vs_actors_stopActorMotion(arg0: c_int) callconv(.c) void {
    const temp_s0 = psx.at(v.g.vs_actors_actorRecords(), arg0).*;
    if (temp_s0) |t| {
        c.vs_actors_clearActorMovementModes(arg0);
        t.unk0.translationFrames = 0;
        t.unk0.turnFrames = 0;
        @as(*align(1) c_int, @ptrCast(&t.unk0.unk34)).* = 0;
    }
}
