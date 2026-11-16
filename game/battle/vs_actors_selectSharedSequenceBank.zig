//! Battle: stores the selection byte vs_actors_sharedSequenceBankIndex and clears the flagged entries of the battle actors (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: vs_actors_selectSharedSequenceBank (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Handles the actors whose slot bytes match index, then stores index - 1 in vs_actors_sharedSequenceBankIndex; index must be 1..3.
pub fn vs_actors_selectSharedSequenceBank(index: c_uint) callconv(.c) c_int {
    if (index -% 1 >= 3) return -1;

    var i: usize = 0;
    while (i < 17) : (i += 1) {
        const temp = v.g.vs_actors_actorRecords()[i];
        if (temp) |actor| {
            // The bytes from unk183C: vs_actors_sharedSequenceBankIndex + 2 (unk183E) and index + 1; index is 1..3 here.
            const selOff: isize = @as(isize, v.g.vs_actors_sharedSequenceBankIndex().*) + 2;
            const idxOff: isize = @as(isize, @intCast(index)) + 1;
            if (psx.fieldAt(u8, &actor.unk183C, selOff).* != 0xFF and psx.fieldAt(u8, &actor.unk183C, idxOff).* == 0xFF) {
                if (actor.unk180C[0].currentFrame >= 2) {
                    _ = c.vs_actors_textureFrameMove(@ptrCast(actor), 0, 0);
                }
                if (actor.unk180C[1].currentFrame >= 2) {
                    _ = c.vs_actors_textureFrameMove(@ptrCast(actor), 1, 0);
                }
            }
        }
    }
    v.g.vs_actors_sharedSequenceBankIndex().* = psx.conv(u8, index -% 1);
    return 0;
}
