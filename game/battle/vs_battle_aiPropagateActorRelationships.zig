//! Reaction targeting: classifies every actor against a reacting state and,
//! for a state with flag21 set, queues the enemies that may still be struck
//! (pc/src/game/BATTLE/BATTLE.PRG/74088.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_74088");
const c = @import("c_74088");

/// orig: vs_battle_aiPropagateActorRelationships (pc/src/game/BATTLE/BATTLE.PRG/74088.c)
/// Classifies the battle actors for one reacting state and queues the targets that need it.
pub fn vs_battle_aiPropagateActorRelationships(state: ?*v.vs_battle_aiState) callconv(.c) void {
    const st = state.?;
    var id: c_int = undefined;
    var classification: c_int = undefined;

    if (st.flag13 != 0) {
        var actor: ?*v.vs_battle_actor = @ptrCast(v.g.vs_battle_actors()[0]);
        while (actor) |a| {
            id = a.id;
            const firstOther: ?*v.vs_battle_aiState = @ptrCast(psx.at(v.g.vs_battle_aiActorAiStates(), id).*);
            if (id == @as(c_int, st.id) or id == 0 or firstOther.?.f2C == 3) {
                classification = 2;
            } else if (firstOther.?.flag13 == 0) {
                if ((@as(c_int, firstOther.?.tbl.t.targets.reaction[0].b.flags) >> 6) == 3) {
                    classification = 3;
                    psx.setBits(&psx.at(&st.tbl.t.targets.reaction, id).p, "counter", 0);
                } else {
                    classification = 1;
                }
            } else {
                classification = 1;
            }
            psx.setBits(&psx.at(&st.tbl.t.targets.reaction, id).p, "classification", psx.conv(u2, classification));
            actor = psx.get(a.next);
        }
    } else if (st.f21 != 0) {
        var actor: ?*v.vs_battle_actor = @ptrCast(v.g.vs_battle_actors()[0]);
        while (actor) |a| {
            const otherClass: c_uint = @intCast(@as(c_int, psx.at(&st.tbl.t.targets.reaction, a.id).b.flags) >> 6);
            const other: ?*v.vs_battle_aiState = @ptrCast(psx.at(v.g.vs_battle_aiActorAiStates(), a.id).*);
            if (otherClass == 2 and (@as(c_int, psx.at(&other.?.tbl.t.targets.reaction, st.id).b.flags) >> 6) == 2) {
                var candidate: ?*v.vs_battle_actor = @ptrCast(v.g.vs_battle_actors()[0]);
                while (candidate) |cand| {
                    id = cand.id;
                    if (psx.getBits(&psx.at(&st.tbl.t.targets.reaction, id).p, "classification") == 1 and
                        (psx.at(&st.tbl.t.targets.reaction, id).raw & 1) != 0 and
                        (@as(c_int, psx.at(&other.?.tbl.t.targets.reaction, id).b.flags) >> 6) == 3)
                    {
                        var ally: ?*v.vs_battle_actor = @ptrCast(v.g.vs_battle_actors()[0]);
                        while (ally) |al| {
                            const allyState: ?*v.vs_battle_aiState = @ptrCast(psx.at(v.g.vs_battle_aiActorAiStates(), al.id).*);
                            if ((@as(c_int, psx.at(&st.tbl.t.targets.reaction, al.id).b.flags) >> 6) == 2 and
                                (@as(c_int, psx.at(&allyState.?.tbl.t.targets.reaction, st.id).b.flags) >> 6) == 2 and
                                (psx.getBits(&psx.at(&allyState.?.tbl.t.targets.reaction, id).p, "classification") == 2 or
                                    (psx.getBits(&psx.at(&allyState.?.tbl.t.targets.reaction, id).p, "counter") != 0 and
                                        psx.getBits(&psx.at(&allyState.?.tbl.t.targets.reaction, id).p, "previous") == 2)))
                            {
                                break;
                            }
                            ally = psx.get(al.next);
                        }
                        if (ally == null) {
                            // The state is passed as the C's pointer; the declared type is not
                            // instantiated here (its target array is opaque in c_74088).
                            const Queue = *const fn (?*anyopaque, c_int) callconv(.c) void;
                            const queue: Queue = @ptrCast(&c.vs_battle_aiMakeTargetHostile);
                            queue(@ptrCast(st), id);
                        }
                    }
                    candidate = psx.get(cand.next);
                }
            }
            actor = psx.get(a.next);
        }
    }
}
