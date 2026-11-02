//! An event script opcode that runs a screen transition or a menu to its end (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B92F4 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// By script[1]: 10..14 start a transition (state flag 0xB6 from D_800E9BF0, or 1 to 2 for 14,
/// which also sets D_800F4C69 to 2) and wait for it, clearing flag 0xB6; 13 and 15 start one
/// (flag 0xB6 to 1, or func_8007B9CC) and wait; any other opens menu script[1] and waits for it
/// to close. D_800F4BF8 marks a started step; returns 1 while waiting, 4 when done.
pub fn func_800B92F4(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const busy = v.g.D_800F4BF8();
    switch (script[1]) {
        10, 11, 12 => {
            if (busy.* == 0) {
                c.vs_battle_setStateFlag(0xB6, v.g.D_800E9BF0()[script[1] - 10]);
                c.func_8007B9A0();
                busy.* = 1;
                return 1;
            }
            return finishState(busy);
        },
        14 => {
            if (busy.* == 0) {
                if (c.vs_battle_getStateFlag(0xB6) == 1) c.vs_battle_setStateFlag(0xB6, 2);
                c.func_8007B9A0();
                v.g.D_800F4C69().* = 2;
                busy.* = 1;
                return 1;
            }
            return finishState(busy);
        },
        13 => {
            if (busy.* == 0) {
                c.vs_battle_setStateFlag(0xB6, 1);
                c.func_8007B9A0();
                busy.* = 1;
                return 1;
            }
            return finishTransition(busy);
        },
        15 => {
            if (busy.* == 0) {
                c.func_8007B9CC();
                busy.* = 1;
                return 1;
            }
            return finishTransition(busy);
        },
        else => {
            if (busy.* == 0) {
                _ = c.func_800CACD0(script[1], 2);
                busy.* = 1;
                return 1;
            }
            c.func_800C97BC();
            const menu: *v.D_800F18EC_t = @ptrCast(@alignCast(c.func_800CB66C().?));
            if (menu.unk0 == 2) {
                c.func_800CB114();
                busy.* = 0;
                return 4;
            }
            return 1;
        },
    }
}

/// The C's `finishState` label: wait for the transition, then clear state flag 0xB6.
fn finishState(busy: *u8) c_int {
    if (c.func_8007B9FC() != 0) return 1;
    c.vs_battle_setStateFlag(0xB6, 0);
    busy.* = 0;
    return 4;
}

/// The C's `finishTransition` label: wait for the transition.
fn finishTransition(busy: *u8) c_int {
    if (c.func_8007B9FC() == 0) {
        busy.* = 0;
        return 4;
    }
    return 1;
}
