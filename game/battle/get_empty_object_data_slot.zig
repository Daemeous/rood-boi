//! Finds the first free object data slot in BATTLE (src/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");

/// orig: vs_battle_getEmptyObjectDataSlot (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Returns the index of the first object data slot with no data type, or -1 if all 16 are in use.
pub fn getEmptyObjectDataSlot() callconv(.c) c_int {
    const slots = v.g.vs_battle_objectDataSlots();
    var i: c_int = 0;
    // i < 16 bounds the loop, so the increment cannot overflow.
    while (i < 16) : (i +%= 1) {
        if (slots[@intCast(i)].dataType == 0) {
            return i;
        }
    }
    return -1;
}
