//! Runs the pending target checks of a battle action state, then clears each actor's target mode
//! (pc/src/game/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: func_800DAAD8 (pc/src/game/BATTLE/BATTLE.PRG/71618.c)
/// Runs the action's pending target checks and tests every actor's targeting category; returns 1 when one of them stops the action.
pub fn func_800DAAD8(state: ?*v.vs_battle_aiState) callconv(.c) c_int {
    const s = state.?;
    const entry = psx.get(s.entries.da4bc[0]);

    if (s.status == 0 and c.vs_battle_aiTryApproachPlannedTarget(@ptrCast(s), @ptrCast(entry)) != 0)
        return 1;
    const target: c_int = s.target16;
    if (target < 16) {
        if (c.vs_battle_aiApproachActor(@ptrCast(s), target, 384, 0) != 0) {
            if (s.rangeMode == 1 and s.mode12E != 7) {
                const et: *v.da4bcEntryTarget = @ptrCast(&entry.?.flagsC);
                s.movementTargetId = psx.getBits(et, "target");
                c.vs_battle_aiReleaseActorAiReservations(@ptrCast(s));
                c.vs_battle_aiRecoverMovement(@ptrCast(s));
            }
            return 1;
        }
        if (c.vs_battle_aiTrackActorPosition(@ptrCast(s), target) != 0)
            return 1;
    }

    const ts: *v.vs_battle_aiState = @ptrCast(s);
    var actor: ?*v.vs_battle_actor = @ptrCast(v.g.vs_battle_actors()[0]);
    while (actor) |a| : (actor = psx.get(a.next)) {
        if (psx.getBits(psx.at(&ts.tbl.t.targets.da4bc, a.id), "mode") != 3 and
            psx.getBits(psx.at(&ts.tbl.t.targets.da4bc, a.id), "category") != 0)
        {
            psx.setBits(psx.at(&ts.tbl.t.targets.da4bc, a.id), "mode", 3);
            if (c.vs_battle_aiFleeVisibleEnemies(@ptrCast(s), 0) != 0) {
                psx.setBits(psx.at(&ts.tbl.t.targets.da4bc, a.id), "mode", psx.getBits(psx.at(&ts.tbl.t.targets.da4bc, a.id), "oldMode"));
                return 1;
            }
            psx.setBits(psx.at(&ts.tbl.t.targets.da4bc, a.id), "mode", psx.getBits(psx.at(&ts.tbl.t.targets.da4bc, a.id), "oldMode"));
        }
    }
    return 0;
}
