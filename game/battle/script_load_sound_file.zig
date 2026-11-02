//! Script opcode: loads the music slot and sound file for the id from func_80089104 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: vs_battle_script_loadSoundFile (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Loads the music slot and the sound file for the id the script gets from func_80089104.
pub fn script_loadSoundFile(_: [*c]u8, _: c_short) callconv(.c) c_int {
    const id: i16 = psx.conv(i16, c.func_80089104());
    _ = c.vs_main_loadMusicSlot(@as(c_int, id), 3);
    c.vs_main_loadSoundFile(@as(c_int, id));
    return 0;
}
