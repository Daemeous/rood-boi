//! Script opcode that loads a sound effect slot (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_battle_script_loadSfxSlot (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Loads the sound effect slot named by the two script bytes after the opcode; returns 0.
pub fn script_loadSfxSlot(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // arg0[1] promotes to int; arg0[2] converts to the prototype's unsigned int.
    _ = c.vs_main_loadSfxSlot(@as(c_int, arg0[1]), @as(c_uint, arg0[2]));
    return 0;
}
