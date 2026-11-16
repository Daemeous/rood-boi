//! Battle unit 4A0A8: forwards the byte at arg0[1] to vs_actors_setSharedSequenceBank
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_event_scriptSelectObjectTextureBank (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls vs_actors_setSharedSequenceBank with the byte at arg0[1] and returns 0.
pub fn vs_event_scriptSelectObjectTextureBank(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_actors_setSharedSequenceBank(@as(c_int, arg0[1]));
    return 0;
}
