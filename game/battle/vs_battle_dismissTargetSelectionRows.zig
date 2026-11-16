//! Marks two lists of battle slots (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_dismissTargetSelectionRows (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Calls vs_battle_updateTargetSelectionHighlight(1), then sets unk0 of the slots in each index list: 3 for list 0, 2 for list 1.
pub fn vs_battle_dismissTargetSelectionRows() callconv(.c) void {
    c.vs_battle_updateTargetSelectionHighlight(1);

    // The index lists are u8 entries read with the C's unchecked indexing:
    // the row index i may pass 16 and the slot index may pass 24.
    var i: c_int = 0;
    while (i < @as(c_int, v.g.vs_battle_firstColumnEntryCount().*)) : (i +%= 1) {
        const slot = psx.at(&v.g.vs_battle_targetColumnEntries()[0], i).*;
        psx.at(&v.g.vs_battle_targetSphere().*.?.unk990, slot).unk0 = 3;
    }

    i = 0;
    while (i < @as(c_int, v.g.vs_battle_secondColumnEntryCount().*)) : (i +%= 1) {
        const slot = psx.at(&v.g.vs_battle_targetColumnEntries()[1], i).*;
        psx.at(&v.g.vs_battle_targetSphere().*.?.unk990, slot).unk0 = 2;
    }
}
