//! Finds the battle slot entry whose first byte matches `arg0 + 1`
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");

/// orig: vs_actors_findRememberedEnemy (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the last of the current block's eight 40-byte entries whose first byte is `arg0 + 1`, or null.
pub fn vs_actors_findRememberedEnemy(arg0: c_int) callconv(.c) ?*anyopaque {
    var found: ?*anyopaque = null;
    // vs_state_currentRoomStateSlot is not checked against the array's bounds, as in the original.
    const block = psx.at(v.g.D_80061078(), v.g.vs_state_currentRoomStateSlot().*);
    var i: usize = 0;
    while (i < 8) : (i += 1) {
        if (@as(c_int, block.unk8[i][0]) == arg0 +% 1) {
            found = &block.unk8[i];
        }
    }
    return found;
}
