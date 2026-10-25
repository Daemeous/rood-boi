//! Stores a battle object data record in the first free slot of the 16-slot table (src/BATTLE/BATTLE.PRG/30DB0.c).
const v = @import("views_30DB0");

/// orig: vs_battle_populateDataSlot (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies `objectData` into the first slot whose dataType is 0 and returns its index, or -1 if none is free.
pub fn populateDataSlot(objectData: ?*v.vs_battle_objectData) callconv(.c) c_int {
    const slots = v.g.vs_battle_objectDataSlots();
    for (slots, 0..) |*entry, i| {
        if (entry.dataType == 0) {
            entry.* = objectData.?.*;
            return @intCast(i);
        }
    }
    return -1;
}
