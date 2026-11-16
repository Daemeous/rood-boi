//! Scales the two bytes of entry arg0 in the vs_actors_actorRecords table from arg1 and arg2 (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: vs_actors_setActorWalkRunSpeeds (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores arg1 and arg2 divided by ONE into entry arg0's bytes, forcing the second to 0xFF when arg1 >= arg2.
pub fn vs_actors_setActorWalkRunSpeeds(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) void {
    // arg0 is not checked against the table's 17 entries: the C does not check either.
    const temp_a0 = psx.at(v.g.vs_actors_actorRecords(), arg0).*;
    if (temp_a0) |t| {
        t.unk5BA = psx.conv(u8, @divTrunc(arg1, 4096));
        t.unk5B9 = psx.conv(u8, @divTrunc(arg2, 4096));
        if (arg1 >= arg2) {
            t.unk5B9 = 0xFF;
        }
    }
}
