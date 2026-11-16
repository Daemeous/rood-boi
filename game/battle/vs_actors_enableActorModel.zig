//! Marks a battle actor's slot as flagged (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_enableActorModel (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets bit 0 of the flags of battle actor `arg0`, if its slot is occupied.
pub fn vs_actors_enableActorModel(arg0: c_int) callconv(.c) void {
    if (psx.at(v.g.vs_battle_actors(), arg0).* != null) {
        c.vs_actors_setActorDisplayFlags(arg0, psx.conv(c_int, c.vs_actors_actorStateFlags(arg0) | 1));
    }
}
