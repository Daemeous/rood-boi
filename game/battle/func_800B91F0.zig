//! Reads a 16-bit value at offset 1 of a script operand and passes it on with
//! the byte at offset 3 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: func_800B91F0 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Looks up the short at offset 1 and calls vs_actors_playActorWeaponDrawSound with it and the byte at offset 3.
pub fn func_800B91F0(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_actors_playActorWeaponDrawSound(c.vs_event_resolveScriptActorReference(psx.conv(u16, c.vs_battle_getShort(arg0 + 1))), @as(c_int, arg0[3]));
    return 0;
}
