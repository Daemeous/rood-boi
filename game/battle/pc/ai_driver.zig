//! The per-frame AI driver (D2: 7C7E0.s; oracle pc/src/battle_helpers.c).
const c = @import("c_battle_helpers");
const v = @import("views_battle_helpers");

/// orig: func_800E511C (pc/src/battle_helpers.c)
/// Per-frame AI driver: unless D_800F58B8 is clear, runs vs_battle_aiCacheActorAiWorkingState, steps
/// D_800F58D4 to the next set bit of D_800F590C (modulo 16), then runs
/// vs_battle_aiUpdatePerceivingActor, vs_battle_aiUpdateActorDecisions, vs_battle_aiAnimateEngagementPhase and vs_battle_aiSaveMovementRandomState. The
/// assembly's table walk and stack switch are not reproduced. An empty mask
/// is reported through pc_unimplemented (the original would spin forever),
/// and the step then spins forever too.
pub fn runAiDriver() callconv(.c) void {
    if (v.g.D_800F58B8().* == 0) return;
    c.vs_battle_aiCacheActorAiWorkingState();
    if (v.g.D_800F590C().* & 0xffff == 0) c.pc_unimplemented("AI driver with no active actors");
    var id: c_int = v.g.D_800F58D4().*;
    while (true) {
        id = (id +% 1) & 15;
        if (v.g.D_800F590C().* & (@as(c_uint, 1) << @intCast(id)) != 0) break;
    }
    v.g.D_800F58D4().* = id;
    c.vs_battle_aiUpdatePerceivingActor();
    c.vs_battle_aiUpdateActorDecisions();
    c.vs_battle_aiAnimateEngagementPhase();
    c.vs_battle_aiSaveMovementRandomState();
}
