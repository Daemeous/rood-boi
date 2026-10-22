//! Plays the invalid-action sound effect (src/BATTLE/BATTLE.PRG/573B8.c).
const c = @import("c_573B8");

/// orig: vs_battle_playInvalidSfx (src/BATTLE/BATTLE.PRG/573B8.c)
/// Plays the invalid-action sound effect (sound 0x7E, default parameter 7).
pub fn playInvalidSfx() callconv(.c) void {
    c.vs_main_playSfxDefault(0x7E, 7);
}
