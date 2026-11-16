//! An event script opcode that sets or clears actor 0's 0x1800 flags, with an animation (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptSetAshleyBattleMode (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Actor 0's stance by script[1] (set or clear): when idle (vs_event_weaponTransitionPending 0) and state flag 0xAC is
/// set, vs_battle_setActorBattleMode with D_800E9B4C[script[1]], and outside vs_event_skipState 2 with script[2] set the
/// animation (vs_actors_playActorAnimation) and vs_actors_playActorWeaponDrawSound, waiting (vs_event_weaponTransitionPending 1, returns 1); while waiting,
/// vs_event_skipState 2 ends it at once, else once vs_actors_actorStateFlags bit 0x80 is set it plays the end
/// animation and returns 0. Otherwise actor 0's flags 0x1800 are set or cleared by script[1]
/// (vs_actors_setActorDisplayFlags, then vs_actors_resetActorWeaponVisibilityBlend).
pub fn vs_event_scriptSetAshleyBattleMode(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const waiting = v.g.vs_event_weaponTransitionPending();
    if (waiting.* == 0) {
        if (c.vs_battle_getStateFlag(0xAC) == 0) return 0;
        c.vs_battle_setActorBattleMode(0, v.g.D_800E9B4C()[script[1]]);
        if (v.g.vs_event_skipState().* != 2 and script[2] != 0) {
            _ = c.vs_actors_playActorAnimation(0, (@as(c_int, script[2]) - 1) * 2 + ((@as(c_int, script[1]) ^ 1) + 0x6D), 0, 10);
            c.vs_actors_playActorWeaponDrawSound(0, @as(c_int, script[1]) ^ 1);
            waiting.* = 1;
            return 1;
        }
    } else if (v.g.vs_event_skipState().* == 2) {
        waiting.* = 0;
    } else {
        if (c.vs_actors_actorStateFlags(0) & 0x80 == 0) return 1;
        _ = c.vs_actors_playActorAnimation(0, if (script[1] != 0) 0x65 else 6, 0, 10);
        waiting.* = 0;
        return 0;
    }
    var flags: c_uint = c.vs_actors_actorStateFlags(0);
    if (script[1] != 0) {
        flags = (flags | 0x1800) & 0xFFFF;
    } else {
        flags &= 0xE7FF;
    }
    c.vs_actors_setActorDisplayFlags(0, psx.conv(c_int, flags));
    c.vs_actors_resetActorWeaponVisibilityBlend(0);
    return 0;
}
