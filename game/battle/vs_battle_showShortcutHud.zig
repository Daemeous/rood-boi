//! Sets the battle global vs_battle_shortcutHudRequested to 1 (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const v = @import("views_5BF94");

/// orig: vs_battle_showShortcutHud (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Sets the global vs_battle_shortcutHudRequested to 1.
pub fn vs_battle_showShortcutHud() callconv(.c) void {
    v.g.vs_battle_shortcutHudRequested().* = 1;
}
