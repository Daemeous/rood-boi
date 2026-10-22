//! Plays sound effect 0x7E as a default sound effect (src/BATTLE/BATTLE.PRG/573B8.c).
const c = @import("c_573B8");

/// orig: vs_battle_playSfx10 (src/BATTLE/BATTLE.PRG/573B8.c)
/// Plays battle sound effect 10: sound 0x7E through the default SFX path.
pub fn playSfx10() callconv(.c) void {
    c.vs_main_playSfxDefault(0x7E, 10);
}
