//! Calls vs_actors_setActorYawTurn for one battle entry with its unk5C0 field set to a value for the duration of the call (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const psx = @import("common").psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_turnActorByAtRate (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Calls vs_actors_setActorYawTurn for entry `arg0` with its unk5C0 set to `arg2` for the call, then restores it.
pub fn vs_actors_turnActorByAtRate(arg0: c_int, arg1: c_short, arg2: c_int) callconv(.c) void {
    // The entry is read before the test; it is only dereferenced on the path that uses it.
    const entry: ?*v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), arg0).*;

    if (arg2 == -1) {
        c.vs_actors_setActorYawTurn(arg0, arg1, -1);
        return;
    }

    const e = entry.?;
    const saved: u16 = e.unk5C0;
    e.unk5C0 = psx.conv(u16, arg2);

    c.vs_actors_setActorYawTurn(arg0, arg1, -1);

    e.unk5C0 = saved;
}
