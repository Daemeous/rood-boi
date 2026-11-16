//! Battle unit 146C: forwards its argument to vs_actors_selectSharedSequenceBank (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_146C");

/// orig: vs_actors_setSharedSequenceBank (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Passes its argument on to vs_actors_selectSharedSequenceBank and discards the result.
pub fn vs_actors_setSharedSequenceBank(arg0: c_int) callconv(.c) void {
    _ = c.vs_actors_selectSharedSequenceBank(psx.conv(c_uint, arg0));
}
