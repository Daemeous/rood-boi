//! Returns the byte at vs_actors_sharedSequenceBankIndex plus one (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");

/// orig: vs_actors_sharedSequenceBankId (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Returns the byte at vs_actors_sharedSequenceBankIndex plus one.
pub fn vs_actors_sharedSequenceBankId() callconv(.c) c_int {
    // The byte is widened to c_int before the add, so 255 + 1 cannot overflow.
    return @as(c_int, v.g.vs_actors_sharedSequenceBankIndex().*) + 1;
}
