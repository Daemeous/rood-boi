//! Menu selection sound effect (src/BATTLE/BATTLE.PRG/573B8.c).
const c = @import("c_573B8");

/// orig: vs_battle_playMenuSelectSfx (src/BATTLE/BATTLE.PRG/573B8.c)
/// Plays the menu selection sound effect (sfx 0x7E) through the default SFX path.
pub fn playMenuSelectSfx() callconv(.c) void {
    c.vs_main_playSfxDefault(0x7E, 5);
}
