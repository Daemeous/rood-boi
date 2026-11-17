//! An event script opcode over an actor set (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B7428 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8007C348(actor, script[3]) for each actor of the script's reference getShort(script + 1).
pub fn func_800B7428(script: [*c]u8, _: c_short) callconv(.c) c_int {
    var set: v.vs_event_ActorSelection = undefined;
    c.vs_event_buildScriptActorSelection(psx.conv(u16, c.vs_battle_getShort(script + 1)), psx.cptr(c.vs_event_ActorSelection, &set));
    while (true) {
        const actor: c_int = c.vs_event_nextScriptSelectedActor(psx.cptr(c.vs_event_ActorSelection, &set));
        if (actor == 1) break;
        c.func_8007C348(actor, script[3]);
    }
    return 0;
}
