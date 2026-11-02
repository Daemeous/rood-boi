//! Battle script opcode: loads the sound file named by the script's first argument byte (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: vs_battle_script_loadSoundFile2 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Loads the sound file whose index is the byte after the opcode; always returns 0.
pub fn script_loadSoundFile2(script: [*c]u8, _: c_short) callconv(.c) c_int {
    c.vs_main_loadSoundFile(@as(c_int, script[1]));
    return 0;
}
