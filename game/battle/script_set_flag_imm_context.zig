//! Script opcode helper: stores the state flag named by the short at `arg0 + 1`
//! in vs_event_cachedFlagValue (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_battle_script_setFlagImmContext (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Reads a short operand from the script and stores the flag it names in vs_event_cachedFlagValue.
pub fn script_setFlagImmContext(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const flag = c.vs_battle_getStateFlag(c.vs_battle_getShort(arg0 + 1));
    v.g.vs_event_cachedFlagValue().* = psx.conv(u8, flag);
    return 0;
}
