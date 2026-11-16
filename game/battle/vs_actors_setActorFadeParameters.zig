//! Stores three bytes of a battle record in the vs_actors_actorRecords table slot `arg0`
//! (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: vs_actors_setActorFadeParameters (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies three bytes of `arg1` into the record at slot `arg0`; -1 if the slot is empty.
pub fn vs_actors_setActorFadeParameters(arg0: c_int, arg1: ?*v.D_800F4538_unk58_2) callconv(.c) c_int {
    // The C does not check arg0 against the 17 slots: psx.at computes the address as C does.
    const temp_a0 = psx.at(v.g.vs_actors_actorRecords(), arg0).*;

    if (temp_a0) |t| {
        t.unk6F2 = arg1.?.unk0;
        t.unk6F1 = arg1.?.unk1;
        t.unk6F0 = arg1.?.unk2;
        return 0;
    }
    return -1;
}
