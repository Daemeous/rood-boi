//! Counts the battle actors in the actor table whose allegiance matches a value (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_actors_countActorsByDisposition (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Counts the occupied battle actor slots whose allegiance equals `arg0`.
pub fn vs_actors_countActorsByDisposition(arg0: c_int) callconv(.c) c_int {
    const actors = v.g.vs_battle_actors();
    var count: c_uint = 0;
    var i: usize = 0;
    // The loop bound is the 16 slots of vs_battle_actors.
    while (i < 16) : (i += 1) {
        if (actors[i]) |actor| {
            if (@as(c_int, actor.allegiance) == arg0) {
                count +%= 1;
            }
        }
    }
    return psx.conv(c_int, count);
}
