//! Battle actor slot step after a turn: removes a defeated actor or updates a live one
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_resolveActorDefeat (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Handles battle actor slot arg1: a defeated actor is removed (slot 0 ends the battle, returning 1), a live one is updated.
pub fn vs_battle_resolveActorDefeat(arg0: c_int, arg1: c_int, arg2: c_int) callconv(.c) c_int {
    var ret: c_int = 0;
    if (psx.at(v.g.vs_battle_actors(), arg1).* != null) {
        const actor = psx.at(v.g.vs_battle_actors(), arg1).*.?;
        if (psx.get(actor.stats).?.currentHP == 0) {
            _ = c._enemyDefeated(arg0, arg1, arg2);
            if (arg1 == 0) {
                c.vs_battle_suppressAshleyStatusLabels();
                _ = c.vs_event_evaluateRoomEventTriggers(0x40, 0, 0, 0);
                c.func_80073898();
                ret = 1;
            } else if (psx.at(v.g.vs_battle_actors(), arg1).*.?.unk26 == 0) {
                _ = c._removeActorAtIndex(psx.conv(c_uint, arg1), 1);
            }
        } else {
            c.func_8009E5C4(arg1);
        }
    }
    return ret;
}
