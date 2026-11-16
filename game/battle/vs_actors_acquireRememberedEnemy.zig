//! Picks the slot of the current zone's table that holds an entry for `arg0`, or claims the next round-robin slot (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_actors_acquireRememberedEnemy (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the current zone's slot holding id `arg0`, claiming the next round-robin slot when none does.
pub fn vs_actors_acquireRememberedEnemy(arg0: c_int) callconv(.c) ?*anyopaque {
    const zone = psx.at(v.g.D_80061078(), v.g.vs_state_currentRoomStateSlot().*);
    var found: ?*[40]u8 = null;

    var i: usize = 0;
    while (i < 8) : (i += 1) {
        // arg0 + 1 wraps (-fwrapv).
        if (@as(c_int, zone.actorStates[i][0]) == arg0 +% 1) {
            found = &zone.actorStates[i];
            break;
        }
    }

    if (found == null) {
        // unk4 is not bounded by the C: the index is computed without a check.
        found = psx.at(&zone.actorStates, psx.conv(c_int, zone.nextSlot));
        zone.nextSlot +%= 1;
        if (zone.nextSlot >= 8) {
            zone.nextSlot = 0;
        }
    }

    return @ptrCast(found);
}
