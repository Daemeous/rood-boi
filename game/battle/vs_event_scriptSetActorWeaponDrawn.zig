//! Walks the record at arg0 + 1 in the battle script, calling vs_battle_setActorBattleMode for each value
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_scriptSetActorWeaponDrawn (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Reads a record from arg0 + 1 and calls vs_battle_setActorBattleMode with each entry, indexed by arg0[3].
pub fn vs_event_scriptSetActorWeaponDrawn(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    var sp10: v.vs_event_ActorSelection = undefined;
    const record: [*c]u8 = @ptrFromInt(@intFromPtr(arg0) +% 1);
    c.vs_event_buildScriptActorSelection(psx.conv(u16, c.vs_battle_getShort(record)), psx.cptr(c.vs_event_ActorSelection, &sp10));
    while (true) {
        const temp: c_int = c.vs_event_nextScriptSelectedActor(psx.cptr(c.vs_event_ActorSelection, &sp10));
        if (temp == 1) break;
        c.vs_battle_setActorBattleMode(temp, v.g.D_800E9B4C()[arg0[3]]);
    }
    return 0;
}
