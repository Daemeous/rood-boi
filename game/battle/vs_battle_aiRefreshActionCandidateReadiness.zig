//! Re-checks an action candidate list: drops candidates that no longer apply,
//! promotes a live one into slot 0 when slot 0 is dead, and sets the list status
//! (pc/src/game/BATTLE/BATTLE.PRG/6FC00.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_6FC00");
const c = @import("c_6FC00");

/// orig: vs_battle_aiRefreshActionCandidateReadiness (pc/src/game/BATTLE/BATTLE.PRG/6FC00.c)
/// Prunes the candidate list, promotes a live candidate into slot 0 when slot 0 is dead, and sets the list status; returns whether slot 0 was replaced.
pub fn vs_battle_aiRefreshActionCandidateReadiness(state: ?*v.vs_battle_aiState) callconv(.c) c_int {
    const st = state.?;
    var changed: c_int = 0;
    var value: u8 = undefined;

    var i: c_int = 0;
    while (i < st.count470) : (i +%= 1) {
        const entry = psx.get(psx.at(&st.entries.candidates, i).*).?;
        if (psx.getBits(entry, "weight") != 0 and
            @as(c_int, psx.getBits(entry, "threshold")) <= @as(c_int, v.g.vs_battle_aiRoomAiState().*.?.unk18))
        {
            const targetId: c_int = @as(c_int, psx.getBits(entry, "targetId"));
            if (c.func_800E4CF4(@ptrCast(st), @ptrCast(entry), targetId) == 0 or
                c.vs_battle_aiRefreshAiActionUsability(
                    @ptrCast(st),
                    @as(c_int, psx.getBits(entry, "row")),
                    @as(c_int, psx.getBits(entry, "column")),
                ) == 0)
            {
                psx.setBits(entry, "weight", 0);
            }
        }
    }

    done: {
        // Slot 0 is only read when the list is non-empty, as in the C.
        if (st.count470 > 0 and psx.getBits(psx.get(st.entries.candidates[0]).?, "weight") == 0) {
            changed = 1;
            var j: c_int = 1;
            while (j < st.count470) : (j +%= 1) {
                const candidate = psx.get(psx.at(&st.entries.candidates, j).*);
                if (psx.getBits(candidate.?, "weight") != 0) {
                    psx.put(&st.entries.candidates[0], candidate);
                    c.vs_battle_aiSelectActionMovementMode(@ptrCast(st));
                    break :done;
                }
            }
            st.count470 = 0;
        }
    }

    if (st.count470 == 0) {
        var actor: ?*v.vs_battle_actor = v.g.vs_battle_actors()[0];
        while (actor) |a| {
            if ((@as(c_int, psx.at(&st.tbl.t.targets.candidate, a.id).flags) >> 6) == 3) break;
            actor = psx.get(a.next);
        }
        value = 2;
        if (actor == null) value = 3;
    } else {
        value = @intFromBool(
            @as(c_int, v.g.vs_battle_aiRoomAiState().*.?.unk18) +% 30 <
                @as(c_int, psx.getBits(psx.get(st.entries.candidates[0]).?, "threshold")),
        );
    }

    st.status = value;
    return changed;
}
