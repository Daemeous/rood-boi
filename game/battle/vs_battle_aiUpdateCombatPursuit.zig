//! The action candidate's target search for its current status
//! (pc/src/game/BATTLE/BATTLE.PRG/71618.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_71618");
const c = @import("c_71618");

/// orig: vs_battle_aiUpdateCombatPursuit (pc/src/game/BATTLE/BATTLE.PRG/71618.c)
/// Runs the target search for the candidate's status and returns 1 when an action was chosen.
pub fn vs_battle_aiUpdateCombatPursuit(state: ?*v.vs_battle_aiState) callconv(.c) c_int {
    const st = state.?;
    const cst: ?*c.struct_vs_battle_aiState = @ptrCast(st);
    var id: c_int = -1;
    var flag: c_int = 0;
    var distance: c_int = 0;
    var nearest: c_uint = 0xFFFF_FFFF;

    sw: switch (@as(c_int, st.status)) {
        0 => {
            var entry = psx.get(st.entries.candidates[0]).?;
            id = @as(c_int, psx.getBits(entry, "targetId"));
            if (c.vs_battle_aiMoveForActionCandidate(@ptrCast(st), @ptrCast(entry)) != 0) return 1;
            var i: c_int = 1;
            while (i < st.count470) : (i +%= 1) {
                entry = psx.get(psx.at(&st.entries.candidates, i).*).?;
                if (@as(c_int, psx.getBits(entry, "threshold")) < @as(c_int, v.g.vs_battle_aiRoomAiState().*.?.unk18) and
                    @as(c_int, psx.getBits(entry, "targetId")) != id)
                {
                    return c.vs_battle_aiMoveForActionCandidate(@ptrCast(st), @ptrCast(entry));
                }
            }
            return 0;
        },
        1 => {
            c.vs_battle_aiDisengageActor(@ptrCast(st));
            if ((@as(c_int, st.tbl.t.targets.candidate[0].flags) >> 6) == 3) {
                if (c.vs_battle_aiApproachActor(cst, 0, 640, 1) != 0) return 1;
                if (c.vs_battle_aiTrackActorPosition(cst, 0) != 0) return 1;
            }
            // targetId is a 4-bit field: always below 16, the targets array's length.
            id = @as(c_int, psx.getBits(psx.get(st.entries.candidates[0]).?, "targetId"));
            if ((@as(c_int, st.tbl.t.targets.candidate[@intCast(id)].flags) >> 6) == 3) {
                flag = 1;
                distance = 640;
            } else {
                if (st.count470 < 2) return 0;
                flag = 0;
                // vs_gte_rsqrt takes a C int: the u32 distance is converted as C converts it.
                distance = c.vs_gte_rsqrt(psx.conv(c_int, psx.get(st.entries.candidates[1]).?.distanceSquared));
            }
            if (c.vs_battle_aiApproachActor(cst, id, distance, flag) != 0) return 1;
            if (c.vs_battle_aiTrackActorPosition(cst, id) != 0) return 1;
            continue :sw 2;
        },
        2 => {
            c.vs_battle_aiDisengageActor(@ptrCast(st));
            if ((@as(c_int, st.tbl.t.targets.candidate[0].flags) >> 6) == 3) {
                if (c.vs_battle_aiApproachActor(cst, 0, 640, 1) != 0) return 1;
                if (c.vs_battle_aiTrackActorPosition(cst, 0) != 0) return 1;
            }
            var actor: ?*v.vs_battle_actor = v.g.vs_battle_actors()[0];
            while (actor) |a| {
                const candidate: c_int = a.id;
                if ((@as(c_int, psx.at(&st.tbl.t.targets.candidate, candidate).flags) >> 6) == 3) {
                    const d: c_uint = psx.at(&st.tbl.t.distances, candidate).*;
                    if (d < nearest) {
                        nearest = d;
                        id = candidate;
                    }
                }
                actor = psx.get(a.next);
            }
            if (id >= 0) {
                if (c.vs_battle_aiApproachActor(cst, id, 640, 1) != 0) return 1;
                if (c.vs_battle_aiTrackActorPosition(cst, id) != 0) return 1;
            }
        },
        else => {},
    }
    return 0;
}
