//! Battle actor HP percentage: current HP as a percent of max, at least 1 when alive (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_actors_actorHpPercentage (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the HP percentage (at least 1) of battle actor `arg0` with live stats, else 0.
pub fn vs_actors_actorHpPercentage(arg0: c_int) callconv(.c) c_int {
    // vs_battle_actors[arg0]->unk3C: the index is not checked, as in the C.
    const temp_a0 = psx.get(psx.at(v.g.vs_battle_actors(), arg0).*.?.stats);

    if (temp_a0) |t| {
        if (t.currentHP > 0) {
            var var_v1: c_int = @divTrunc(@as(c_int, t.currentHP) *% 0x64, @as(c_int, t.maxHP));
            if (var_v1 == 0) {
                var_v1 = 1;
            }
            return var_v1;
        }
    }
    return 0;
}
