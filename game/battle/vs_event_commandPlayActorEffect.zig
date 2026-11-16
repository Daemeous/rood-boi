//! Forwards a short read from the script at `arg0 + 1` through `vs_event_resolveScriptActorReference`
//! into `func_8007BE20` (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: vs_event_commandPlayActorEffect (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Reads two shorts from the script at `arg0` and passes them on; always returns 0.
pub fn vs_event_commandPlayActorEffect(arg0: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = arg1;
    // The second argument's call is made first, as the object orders it.
    const second = c.vs_event_resolveScriptActorReference(psx.conv(c_ushort, c.vs_battle_getShort(psx.at(arg0, 3))));
    c.func_8007BE20(c.vs_battle_getShort(psx.at(arg0, 1)), second);
    return 0;
}
