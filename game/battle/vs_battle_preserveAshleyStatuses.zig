//! Runs vs_battle_removeActorStatuses on the player's stats (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_preserveAshleyStatuses (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_battle_removeActorStatuses(0, ...) with the character's stats block.
pub fn vs_battle_preserveAshleyStatuses() callconv(.c) void {
    c.vs_battle_removeActorStatuses(0, @ptrCast(psx.get(v.g.vs_battle_characterState().*.?.stats)));
}
