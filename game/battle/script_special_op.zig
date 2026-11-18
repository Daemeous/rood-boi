//! The event script's special operations (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
const event_jump = @import("event_jump.zig");

/// orig: vs_battle_script_specialOp (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Runs special operation script[1] with argument script[2]: the ending (1, over two steps),
/// action unlocks (3), the SCREFF2 border (15), a sub-script (16: returns vs_event_specialSubroutineScript's address
/// and keeps script + 6 in vs_event_subroutineReturnCursor), and others; returns 1 while waiting, else 0.
pub fn script_specialOp(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var buf: [32]u8 = undefined;
    switch (script[1]) {
        0 => _ = c.func_8007C8F8(script[2]),
        1 => {
            if (v.g.vs_event_isMenuTransitionActive().* != 0) {
                if (c.vs_battle_updateRewardSequence() != 0) return 1;
                c.vs_battle_setStateFlag(0xB6, 0);
                v.g.vs_event_isMenuTransitionActive().* = 0;
                c.vs_main_showEndingAndReturnToTitle();
            } else {
                c.vs_battle_setStateFlag(0xB6, 3);
                c.vs_battle_beginEventRewardResults();
                v.g.vs_event_isMenuTransitionActive().* = 1;
                return 1;
            }
        },
        2 => v.g.vs_event_isSkipForced().* = 1,
        3 => {
            if (c.vs_battle_isActionUnlocked(script[2]) != 0) {
                c.vs_battle_setStateFlag(0xB1, 0);
            } else {
                c.vs_battle_setActionUnlocked(script[2]);
                c.vs_battle_setStateFlag(0xB1, 1);
            }
        },
        4 => c.vs_battle_setStateFlag(0xB1, psx.conv(u8, c.vs_actors_countActorsByDisposition(4))),
        5 => c.func_800BAF6C(script[2]),
        6 => c.vs_main_freeHeapR(v.g.vs_event_chainedEventBuffer().*),
        7 => c.func_800BBE04(script[2]),
        8 => c.vs_state_setSceneTimerPaused(script[2]),
        9 => c.func_800BE3D0(script[2]),
        // script[2] is not bounded: the table's length is not the C's concern.
        10 => c.vs_field_setCameraPitchPreset(psx.at(v.g.D_800E9C00(), script[2]).*),
        11 => c.func_800BE5A4(script[2]),
        12 => c.vs_field_freezeMovingPlatforms(script[2]),
        13 => c.vs_field_clearGeometryOcclusion(),
        14 => c.vs_field_setMovingPlatformsPaused(script[2]),
        15 => c.pc_screff2_set_border(script[2]),
        16 => {
            v.g.vs_event_subroutineReturnCursor().* = script + 6;
            return event_jump.to(psx.Global(u8, "vs_event_specialSubroutineScript").ptr());
        },
        17 => c.vs_field_cancelPendingRoomVisit(),
        else => _ = c.sprintf(&buf, @constCast("<SPECIAL=%d> IS UNDEFINED!"), @as(c_int, script[1])),
    }
    return 0;
}
