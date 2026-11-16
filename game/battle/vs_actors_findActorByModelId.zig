//! Finds the battle actor slot whose unk18 matches a value (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");

/// orig: vs_actors_findActorByModelId (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Returns the first of the 16 battle actor slots whose unk18 equals `arg0`, or 1 if none does.
pub fn vs_actors_findActorByModelId(arg0: c_int) callconv(.c) c_int {
    var i: c_uint = 0;
    while (i < 16) : (i +%= 1) {
        // i < 16: the slot count, so the index is in range and the cast below is exact.
        const slot = v.g.vs_battle_actors()[i];
        if (slot) |actor| {
            if (@as(c_int, actor.unk18) == arg0) {
                return @intCast(i);
            }
        }
    }
    return 1;
}
