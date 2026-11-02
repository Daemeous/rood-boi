//! Script opcode: clears the music load slot named by the script's first argument byte (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_battle_script_clearMusicLoadSlot (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Clears the music load slot in script byte 1; returns 1 when the clear reports success, else 0.
pub fn script_clearMusicLoadSlot(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const slot: c_uint = arg0[1];
    return @intFromBool(c.vs_main_clearMusicLoadSlot(slot) == 1);
}
