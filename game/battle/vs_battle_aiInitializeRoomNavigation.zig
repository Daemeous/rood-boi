//! Battle overlay unit 7CD68: a forwarding stub (pc/src/game/BATTLE/BATTLE.PRG/7CD68.c).
const c = @import("c_7CD68");

/// orig: vs_battle_aiInitializeRoomNavigation (pc/src/game/BATTLE/BATTLE.PRG/7CD68.c)
/// Forwards to vs_battle_aiInitializeRoomAiMap, which the unit calls through `c` so the owner switch selects it.
pub fn vs_battle_aiInitializeRoomNavigation() callconv(.c) void {
    c.vs_battle_aiInitializeRoomAiMap();
}
