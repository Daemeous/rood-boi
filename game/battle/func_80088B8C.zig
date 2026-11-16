//! Camera-mode tick: counts the target's risk every 30 game ticks while the battle camera is in mode 1 to 3
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: func_80088B8C (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Every 30 ticks, raises the character's risk while the targeted actor is marked (status 8).
pub fn func_80088B8C() callconv(.c) void {
    if (v.g.D_800F196C().* == 2 and v.g._cameraMode().* < 4 and v.g._cameraMode().* != 0) {
        if (v.g.vs_battle_targetingRiskTimer().* == 0 and
            (psx.get(v.g.vs_battle_actors()[@as(usize, v.g.vs_battle_actionState().*.?.unk8.primaryTarget.target.targetActor)].?.stats).?.statuses & 8) != 0 and
            v.g.vs_battle_actionState().*.?.unk8.primaryTarget.unk40 == 0)
        {
            if (v.g.vs_battle_actionState().*.?.unk8.primaryTarget.target.targetActor == 0) {
                const stats = psx.get(v.g.vs_battle_characterState().*.?.stats).?;
                if (stats.risk < 100) {
                    stats.risk +%= 1;
                }
            }
        }
        v.g.vs_battle_targetingRiskTimer().* = @rem(v.g.vs_battle_targetingRiskTimer().* +% v.g.vs_gametime_tickspeed().*, 30);
    }
}
