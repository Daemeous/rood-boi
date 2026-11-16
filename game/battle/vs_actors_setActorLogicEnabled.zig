//! Marks battle actor `arg0` as defeated or revives it (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_setActorLogicEnabled (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Revives actor `arg0` when `arg1` is nonzero, else marks it defeated; returns `arg1`.
pub fn vs_actors_setActorLogicEnabled(arg0: c_int, arg1: c_int) callconv(.c) c_int {
    if (arg1 != 0) {
        if (psx.at(v.g.vs_battle_actors(), arg0).*) |actor| {
            if (actor.unk8 == 0x20) {
                actor.unk8 = 0;
                actor.allegiance &= 0x7FFF;
            }
        }
    } else {
        if (psx.at(v.g.vs_battle_actors(), arg0).*) |actor| {
            actor.unk8 = 0x20;
            actor.allegiance |= 0x8000;
            c.vs_battle_setActorMarker(arg0, 0);
            c.vs_battle_setActorMarker(arg0, 0x40);
        }
    }
    return arg1;
}
