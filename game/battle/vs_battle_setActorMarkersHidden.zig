//! Stores its argument in the global vs_battle_actorMarkersHidden (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");

/// orig: vs_battle_setActorMarkersHidden (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Stores `arg0` in vs_battle_actorMarkersHidden, truncated to 16 bits as the C store does.
pub fn vs_battle_setActorMarkersHidden(arg0: c_int) callconv(.c) void {
    v.g.vs_battle_actorMarkersHidden().* = psx.conv(i16, arg0);
}
