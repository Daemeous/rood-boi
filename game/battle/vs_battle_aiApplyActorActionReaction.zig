//! Applies an action's stat changes to the actor they hit: reaction timers, counters and classifications (pc/src/game/BATTLE/BATTLE.PRG/74088.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

const StateFn = *const fn (?*v.vs_battle_aiState, c_int) callconv(.c) void;
const StateFn1 = *const fn (?*v.vs_battle_aiState) callconv(.c) void;

/// orig: vs_battle_aiApplyActorActionReaction (pc/src/game/BATTLE/BATTLE.PRG/74088.c)
/// Updates an actor's reaction state for each stat-change entry of an action that hit it.
pub fn vs_battle_aiApplyActorActionReaction(state: ?*v.vs_battle_aiState, app: ?*v.actionStatApplication) callconv(.c) void {
    const a = app orelse return;
    if (a.first.skip != 0) return;
    const st = state.?;
    const own: c_int = @as(c_int, st.id);
    if (own == 0) return;

    const action: c_int = @as(c_int, a.action);
    const info: *align(1) v.actionReactionAction = @ptrCast(psx.at(v.g.actions(), action));
    const bad = ((info.effect1 >> 7) & 63) == 3 or ((info.effect2 >> 7) & 63) == 3;
    if (bad) return;

    const source: c_int = @as(c_int, a.first.actor);
    // Unchecked as in the C (a source past the 18 entries reads on).
    const other: ?*v.vs_battle_aiState = @ptrCast(psx.at(v.g.vs_battle_aiActorAiStates(), source).*);
    const count: c_int = @as(c_int, a.count);

    const reactionFn1: StateFn1 = @ptrCast(&c.vs_battle_aiEndAshleyAlliance);
    const reactionFn2: StateFn = @ptrCast(&c.vs_battle_aiTryBeginAshleyAlliance);
    const reactionFn3: StateFn = @ptrCast(&c.vs_battle_aiMakeTargetHostile);

    var i: c_int = 0;
    while (i < count) : (i +%= 1) {
        const entry = psx.at(&a.entries, i);
        if (entry.skip != 0 or @as(c_int, entry.actor) != own or source == own) continue;

        if (st.mode12D == 1) st.mode12D = 2;
        if (c.vs_battle_aiIsActionBeneficialToActor(action, @as(c_int, st.id)) != 0) {
            if (source == 0 and st.flag13 == 0 and st.flag27 != 0) {
                reactionFn2(st, 0);
            } else if ((other.?.tbl.t.targets.reaction[0].b.flags >> 6) == 3) {
                reactionFn1(st);
            }
        }

        const target = psx.at(&st.tbl.t.targets.reaction, source);
        psx.setBits(&target.p, "timer", 28);

        if (c.vs_battle_aiIsActionHarmfulToActor(action, @as(c_int, st.id)) != 0) {
            if (v.g.D_800F5920().* != null and source == 0 and (entry.modes & 3) != 2 and entry.hp > 0) {
                const motion: *v.actionReactionMotion = @ptrCast(v.g.D_800F5920().*.?);
                motion.flag = 0;
            }
            if (source == 0 and st.flag27 != 0) {
                reactionFn1(st);
            }
            if (st.f30 != 0 and (st.flag20 == 0 or source == 0)) {
                reactionFn3(st, source);
            } else if (psx.getBits(&target.p, "classification") != 3) {
                const ot = psx.at(&other.?.tbl.t.targets.reaction, own);
                if (psx.getBits(&ot.p, "counter") != 0) {
                    psx.setBits(&ot.p, "counter", 0);
                    psx.setBits(&ot.p, "classification", psx.getBits(&ot.p, "previous"));
                } else {
                    psx.setBits(&target.p, "counter", 127);
                    psx.setBits(&target.p, "previous", psx.getBits(&target.p, "classification"));
                    if (st.flag20 != 0) {
                        reactionFn3(st, source);
                    }
                }
            }
        }
    }
}
