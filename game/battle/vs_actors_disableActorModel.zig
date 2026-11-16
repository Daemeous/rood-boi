//! Clears bit 0 of a battle actor's slot word when the slot is occupied (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_disableActorModel (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears bit 0 of the word vs_actors_actorStateFlags returns for actor slot `arg0`, if that slot is occupied.
pub fn vs_actors_disableActorModel(arg0: c_int) callconv(.c) void {
    if (psx.at(v.g.vs_battle_actors(), arg0).* != null) {
        c.vs_actors_setActorDisplayFlags(arg0, psx.conv(c_int, c.vs_actors_actorStateFlags(arg0)) & ~@as(c_int, 1));
    }
}
