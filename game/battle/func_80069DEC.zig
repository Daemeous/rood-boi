//! Clears the enemy-table blocks that match a map and zone id (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80069DEC (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears the 320-byte block of every one of the four entries whose map and zone ids match.
pub fn func_80069DEC(mapId: c_int, zndId: c_int) callconv(.c) void {
    const table = v.g.D_80061078();
    var i: usize = 0;
    while (i < 4) : (i += 1) {
        const entry = &table[i];
        if (@as(c_int, entry.mapId) == mapId and @as(c_int, entry.zndId) == zndId) {
            c.vs_main_bzero(&entry.actorStates, @sizeOf(@TypeOf(entry.actorStates)));
        }
    }
}
