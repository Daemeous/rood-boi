//! Clears the 146C counter and its record table (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_clearRoomEnemyMemory (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Zeroes vs_state_currentRoomStateSlot and the four-entry record table at D_80061078.
pub fn vs_actors_clearRoomEnemyMemory() callconv(.c) void {
    v.g.vs_state_currentRoomStateSlot().* = 0;
    c.vs_main_bzero(@ptrCast(v.g.D_80061078()), @intCast(@sizeOf([4]v.vs_state_RoomEnemyStates)));
}
