//! An event script opcode that runs a screen transition or a menu to its end (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B92F4 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// By script[1]: 10..14 start a transition (state flag 0xB6 from vs_event_menuTransitionModes, or 1 to 2 for 14,
/// which also sets vs_event_musicSlot to 2) and wait for it, clearing flag 0xB6; 13 and 15 start one
/// (flag 0xB6 to 1, or vs_battle_beginPostActionRewards) and wait; any other opens menu script[1] and waits for it
/// to close. vs_event_isMenuTransitionActive marks a started step; returns 1 while waiting, 4 when done.
pub fn func_800B92F4(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const busy = v.g.vs_event_isMenuTransitionActive();
    switch (script[1]) {
        10, 11, 12 => {
            if (busy.* == 0) {
                c.vs_battle_setStateFlag(0xB6, v.g.vs_event_menuTransitionModes()[script[1] - 10]);
                c.vs_battle_beginEventRewardResults();
                busy.* = 1;
                return 1;
            }
            return finishState(busy);
        },
        14 => {
            if (busy.* == 0) {
                if (c.vs_battle_getStateFlag(0xB6) == 1) c.vs_battle_setStateFlag(0xB6, 2);
                c.vs_battle_beginEventRewardResults();
                v.g.vs_event_musicSlot().* = 2;
                busy.* = 1;
                return 1;
            }
            return finishState(busy);
        },
        13 => {
            if (busy.* == 0) {
                c.vs_battle_setStateFlag(0xB6, 1);
                c.vs_battle_beginEventRewardResults();
                busy.* = 1;
                return 1;
            }
            return finishTransition(busy);
        },
        15 => {
            if (busy.* == 0) {
                c.vs_battle_beginPostActionRewards();
                busy.* = 1;
                return 1;
            }
            return finishTransition(busy);
        },
        else => {
            if (busy.* == 0) {
                _ = c.vs_menu_frameworkOpenGameMenu(script[1], 2);
                busy.* = 1;
                return 1;
            }
            c.vs_menu_frameworkUpdateMenuOverlay();
            const menu: *v.D_800F18EC_t = @ptrCast(@alignCast(c.vs_menu_frameworkActionSelectionState().?));
            if (menu.unk0 == 2) {
                c.vs_menu_frameworkReleaseMenuRows();
                busy.* = 0;
                return 4;
            }
            return 1;
        },
    }
}

/// The C's `finishState` label: wait for the transition, then clear state flag 0xB6.
fn finishState(busy: *u8) c_int {
    if (c.vs_battle_updateRewardSequence() != 0) return 1;
    c.vs_battle_setStateFlag(0xB6, 0);
    busy.* = 0;
    return 4;
}

/// The C's `finishTransition` label: wait for the transition.
fn finishTransition(busy: *u8) c_int {
    if (c.vs_battle_updateRewardSequence() == 0) {
        busy.* = 0;
        return 4;
    }
    return 1;
}
