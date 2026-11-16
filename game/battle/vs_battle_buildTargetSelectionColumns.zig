//! Clears the four score counters, places the battle groups by how many enemies each holds, and sets the layout spacing (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: vs_battle_buildTargetSelectionColumns (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Places each group by its enemy count, largest sizes first, then sets the spacing from the highest placed index.
pub fn vs_battle_buildTargetSelectionColumns() callconv(.c) void {
    v.g.vs_battle_secondColumnEntryCount().* = 0;
    v.g.vs_battle_firstColumnEntryCount().* = 0;
    v.g.vs_battle_secondColumnGroupCount().* = 0;
    v.g.vs_battle_firstColumnGroupCount().* = 0;
    var size: c_int = 6;
    while (size > 0) : (size -%= 1) {
        var group: c_int = 0;
        while (group < 16) : (group +%= 1) {
            var count: c_int = 0;
            var i: c_int = 0;
            while (i < @as(c_int, v.g.vs_battle_targetSphere().*.?.unk2C)) : (i +%= 1) {
                const entry = psx.at(v.g.vs_battle_targetSphere().*.?.unk40.?, i);
                if (@as(c_int, entry.unk9) == group) count +%= 1;
            }
            if (count == size) c.vs_battle_appendTargetGroupToColumn(group);
        }
    }
    var i: c_int = 0;
    while (i < @as(c_int, v.g.vs_battle_targetSphere().*.?.unk2C)) : (i +%= 1) {
        const group: c_int = @as(c_int, psx.at(v.g.vs_battle_targetSphere().*.?.unk40.?, i).unk9);
        if ((group >> 4) != 0) c.vs_battle_appendTargetGroupToColumn(group);
    }
    var lo: c_int = @as(c_int, v.g.vs_battle_firstColumnEntryCount().*);
    if (lo < @as(c_int, v.g.vs_battle_secondColumnEntryCount().*)) lo = @as(c_int, v.g.vs_battle_secondColumnEntryCount().*);
    v.g.vs_battle_targetColumnTopOffset().* = psx.conv(u8, ((16 -% lo) *% 11) >> 1);
}
