//! Walks the actor timer list and advances the first timer whose phase changed, setting its actor's mode
//! (pc/src/game/BATTLE/BATTLE.PRG/73700.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_73700");

/// orig: vs_battle_aiAnimateEngagementPhase (pc/src/game/BATTLE/BATTLE.PRG/73700.c)
/// Steps the actor timer states from the list head, stopping after the first one whose phase reached an end.
pub fn vs_battle_aiAnimateEngagementPhase() callconv(.c) void {
    const head: ?*v.vs_battle_aiState = @ptrCast(@alignCast(psx.get(v.g.vs_battle_aiRoomAiState().*.?.unk1C)));
    var changed: c_int = 0;
    if (head) |first| {
        var state: *v.vs_battle_aiState = first;
        while (true) {
            const actor = psx.get(state.actor);
            if ((@as(c_int, state.tbl.targetFlag0) >> 6) != 2) {
                if (@as(c_int, state.phase12F) == 0) {
                    state.direction130 = 1;
                    actor.?.unkB = (actor.?.unkB & 0xf3) | (1 << 2);
                    changed = 1;
                } else if (@as(c_int, state.phase12F) == 16) {
                    state.direction130 = -1;
                    actor.?.unkB = (actor.?.unkB & 0xf3) | (2 << 2);
                    changed = 1;
                }
                state.phase12F = psx.conv(i8, @as(c_int, state.phase12F) +% @as(c_int, state.direction130));
                if (changed != 0) break;
            }
            const next = psx.get(state.next1A0);
            if (next == null) break;
            state = next.?;
        }
    }
}
