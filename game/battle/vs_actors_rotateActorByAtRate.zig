//! Runs vs_actors_setActorRotation for battle slot arg0 with its unk5C0 set to arg2 for the call (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_rotateActorByAtRate (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Calls vs_actors_setActorRotation for slot arg0 with its unk5C0 temporarily set to arg2; arg2 == -1 calls it unchanged.
pub fn vs_actors_rotateActorByAtRate(arg0: c_int, arg1: ?*v.SVECTOR, arg2: c_int) callconv(.c) void {
    if (arg2 == -1) {
        c.vs_actors_setActorRotation(arg0, @ptrCast(arg1), -1);
        return;
    }

    // The C indexes vs_actors_actorRecords without a bound check and dereferences the slot unconditionally here.
    const slot = psx.at(v.g.vs_actors_actorRecords(), arg0).*.?;
    const saved = slot.unk5C0;
    slot.unk5C0 = psx.conv(u16, arg2);

    c.vs_actors_setActorRotation(arg0, @ptrCast(arg1), -1);

    slot.unk5C0 = saved;
}
