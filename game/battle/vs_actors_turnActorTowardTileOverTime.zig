//! Forwards a call to vs_actors_setActorTileTurn with the unk5C0 field of the enemy's entry in vs_actors_actorRecords
//! (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_turnActorTowardTileOverTime (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Calls vs_actors_setActorTileTurn with the unk5C0 value of vs_actors_actorRecords[arg0].
pub fn vs_actors_turnActorTowardTileOverTime(arg0: c_int, arg1: ?*v.func_8006EBF8_t_fields, arg2: c_int) callconv(.c) void {
    // vs_actors_actorRecords[arg0] is dereferenced unconditionally in the C: `.?` matches that.
    const unk5C0: c_int = @as(c_int, psx.at(v.g.vs_actors_actorRecords(), arg0).*.?.unk5C0);
    c.vs_actors_setActorTileTurn(arg0, @ptrCast(arg1), arg2, unk5C0);
}
