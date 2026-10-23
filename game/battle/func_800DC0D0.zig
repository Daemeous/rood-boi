//! Walks the actor timer list and advances the first timer whose phase changed, setting its actor's mode
//! (src/BATTLE/BATTLE.PRG/73700.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_73700");

/// orig: func_800DC0D0 (src/BATTLE/BATTLE.PRG/73700.c)
/// Steps the actor timer states from the list head, stopping after the first one whose phase reached an end.
pub fn func_800DC0D0() callconv(.c) void {
    const head: ?*v.actorTimerState = @ptrCast(psx.get(v.g.D_800F58BC().*.?.unk1C));
    var changed: c_int = 0;
    if (head) |first| {
        var state: *v.actorTimerState = first;
        while (true) {
            const actor = psx.get(state.actor);
            if ((@as(c_int, state.targetFlags) >> 6) != 2) {
                if (@as(c_int, state.phase) == 0) {
                    state.direction = 1;
                    psx.setBits(actor.?, "mode", 1);
                    changed = 1;
                } else if (@as(c_int, state.phase) == 16) {
                    state.direction = -1;
                    psx.setBits(actor.?, "mode", 2);
                    changed = 1;
                }
                state.phase = psx.conv(i8, @as(c_int, state.phase) +% @as(c_int, state.direction));
                if (changed != 0) break;
            }
            const next = psx.get(state.next);
            if (next == null) break;
            state = next.?;
        }
    }
}
