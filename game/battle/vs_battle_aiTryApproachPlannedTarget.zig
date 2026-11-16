//! Battle candidate check for a scene entry: runs the candidate test and, when it
//! passes, the two follow-up steps (pc/src/game/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: vs_battle_aiTryApproachPlannedTarget (pc/src/game/BATTLE/BATTLE.PRG/71618.c)
/// Runs the candidate test for an entry and, if it passes, the follow-up steps; returns 1 when one of them succeeds.
pub fn vs_battle_aiTryApproachPlannedTarget(state: ?*v.vs_battle_aiState, entry: ?*v.da4bcEntry) callconv(.c) c_int {
    const result: c_int = 0;
    if (c.vs_battle_aiCanApproachEngagedTarget(@ptrCast(state), @ptrCast(entry)) != 0) {
        const e = entry.?;
        const target: c_int = psx.conv(c_int, e.flagsC & 15);
        const distance: c_int = @bitCast(c.vs_gte_rsqrt(@bitCast(e.distance)));
        const s = state.?;
        var radius: c_int = distance +% (@as(c_int, s.step) << 5);
        radius = radius *% radius;
        v.g.vs_battle_aiRoomAiState().*.?.unk24 +%= 1;
        if (s.tbl.t.distances[@intCast(target)] < psx.conv(c_uint, radius)) {
            if ((s.tbl.t.targets.bits32[@intCast(e.flagsC & 15)] & 1) != 0) {
                c.vs_battle_aiTryEngageActor(@ptrCast(state));
            }
        }
        if (c.vs_battle_aiApproachActor(@ptrCast(state), target, distance, 1) != 0) {
            return 1;
        }
        if (c.vs_battle_aiTrackActorPosition(@ptrCast(state), target) != 0) {
            return 1;
        }
    }
    return result;
}
