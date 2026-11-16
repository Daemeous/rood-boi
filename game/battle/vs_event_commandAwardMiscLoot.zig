//! Battle script step: starts a timed wait on the first call and reports done once it expires (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandAwardMiscLoot (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Starts the wait described by the script bytes on the first call; afterwards returns 1 while it runs and 4 when it has finished.
pub fn vs_event_commandAwardMiscLoot(arg0: [*c]u8) callconv(.c) c_int {
    if (v.g.vs_event_isMenuTransitionActive().* == 0) {
        // GCC evaluates the call arguments right to left: the getShort at arg0 + 4 runs before the one at arg0 + 1.
        const fourth: c_int = @as(c_int, arg0[6]);
        const third: c_int = @as(c_int, c.vs_battle_getShort(arg0 + 4));
        const second: c_int = @as(c_int, arg0[3]);
        const first: c_int = @as(c_int, c.vs_battle_getShort(arg0 + 1));
        c.vs_items_presentScriptedLoot(first, second, third, fourth);
        v.g.vs_event_isMenuTransitionActive().* = 1;
        return 1;
    }

    if (c.vs_battle_updateRewardSequence() == 0) {
        v.g.vs_event_isMenuTransitionActive().* = 0;
        return 4;
    }
    return 1;
}
