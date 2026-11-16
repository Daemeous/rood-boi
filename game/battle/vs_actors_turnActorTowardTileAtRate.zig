//! Passes a slot's value to vs_actors_setActorTileTurn (pc/src/game/BATTLE/BATTLE.PRG/40564.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_40564");
const c = @import("c_40564");

/// orig: vs_actors_turnActorTowardTileAtRate (pc/src/game/BATTLE/BATTLE.PRG/40564.c)
/// Calls vs_actors_setActorTileTurn with arg2, or with the unk5C0 of the slot arg0 when arg2 is -1.
pub fn vs_actors_turnActorTowardTileAtRate(arg0: c_int, arg1: ?*v.func_8006EBF8_t_fields, arg2: c_int) callconv(.c) void {
    if (arg2 == -1) {
        // vs_actors_actorRecords[arg0] is indexed unchecked, as the C does; a null entry traps where the C faults.
        const new_var = psx.at(v.g.vs_actors_actorRecords(), arg0).*.?;
        c.vs_actors_setActorTileTurn(arg0, @ptrCast(arg1), -1, @as(c_int, new_var.unk5C0));
    } else {
        c.vs_actors_setActorTileTurn(arg0, @ptrCast(arg1), -1, arg2);
    }
}
