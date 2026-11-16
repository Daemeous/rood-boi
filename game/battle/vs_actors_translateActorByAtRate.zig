//! Calls the unit's helper `vs_actors_setActorTranslation` with a temporary u16 override at vs_actors_actorRecords[arg0].unk5C2
//! (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_translateActorByAtRate (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Calls vs_actors_setActorTranslation with the entry's unk5C2 set to `arg2` for the call, unless `arg2` is -1, then restores it.
pub fn vs_actors_translateActorByAtRate(arg0: c_int, arg1: ?*v.SVECTOR, arg2: c_int) callconv(.c) void {
    // The entry is read before the test; it is only dereferenced below.
    const temp_s1: ?*v.D_800F4538_t = psx.at(v.g.vs_actors_actorRecords(), arg0).*;

    if (arg2 == -1) {
        c.vs_actors_setActorTranslation(arg0, @ptrCast(arg1), -1);
        return;
    }

    const entry = temp_s1.?;
    const temp_s0: u16 = entry.unk5C2;
    entry.unk5C2 = psx.conv(u16, arg2);

    c.vs_actors_setActorTranslation(arg0, @ptrCast(arg1), -1);

    entry.unk5C2 = temp_s0;
}
