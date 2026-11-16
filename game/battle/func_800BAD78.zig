//! Battle script helper: walks the list read from a script's word at offset 1 and calls vs_actors_setActorLogicEnabled for each entry (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800BAD78 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Walks the list at a script's word 1, calling vs_actors_setActorLogicEnabled for each entry with the script's byte 3.
pub fn func_800BAD78(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    var sp10: v.func_800BB68C_t = undefined;

    c.vs_event_buildScriptActorSelection(psx.conv(u16, c.vs_battle_getShort(psx.at(arg0, 1))), psx.cptr(c.func_800BB68C_t, &sp10));
    while (true) {
        const temp_a0: c_int = @as(c_int, c.vs_event_nextScriptSelectedActor(psx.cptr(c.func_800BB68C_t, &sp10)));
        if (temp_a0 == 1) break;
        _ = c.vs_actors_setActorLogicEnabled(temp_a0, @as(c_int, arg0[3]));
    }
    return 0;
}
