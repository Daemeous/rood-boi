//! Menu leave sound effect (src/BATTLE/BATTLE.PRG/573B8.c).
const c = @import("c_573B8");

/// orig: vs_battle_playMenuLeaveSfx (src/BATTLE/BATTLE.PRG/573B8.c)
/// Plays the menu leave sound effect (0x7E) with the default parameter 6.
pub fn playMenuLeaveSfx() callconv(.c) void {
    c.vs_main_playSfxDefault(0x7E, 6);
}
