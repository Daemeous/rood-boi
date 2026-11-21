//! Copies the VECTOR argument to D_800F4438 and sets every live actor's unk183C from D_800F2458 (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");

/// orig: vs_actors_setRoomActorScale (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Stores the vector into D_800F4438, then copies the vector's x (the low half of its word, which the original read as a u_short past the end of D_800F2458) into each non-null slot's unk183C.
pub fn vs_actors_setRoomActorScale(arg0: ?*v.VECTOR) callconv(.c) void {
    v.g.D_800F4438().* = arg0.?.*;

    const table = v.g.vs_actors_actorRecords();
    var i: usize = 0;
    while (i < 17) : (i += 1) {
        if (table[i]) |slot| {
            slot.unk183C = psx.conv(i16, v.g.D_800F4438().vx);
        }
    }
}
