//! Records the active-list indices of one actor group (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: vs_battle_appendTargetGroupToColumn (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Appends the index of every actor in `group` to one of the two index lists, and records the group's first index and id.
pub fn vs_battle_appendTargetGroupToColumn(group: c_int) callconv(.c) void {
    var first: c_int = 0;
    var i: c_int = 0;
    if (v.g.vs_battle_firstColumnEntryCount().* > v.g.vs_battle_secondColumnEntryCount().*) {
        while (i < v.g.vs_battle_targetSphere().*.?.unk2C) : (i +%= 1) {
            const elem = psx.at(v.g.vs_battle_targetSphere().*.?.unk40.?, i);
            if (@as(c_int, elem.unk9) == group) {
                if (first == 0) first = i +% 1;
                const row: [*]u8 = &v.g.vs_battle_targetColumnEntries().*[1];
                const slot = v.g.vs_battle_secondColumnEntryCount().*;
                v.g.vs_battle_secondColumnEntryCount().* +%= 1;
                psx.at(row, slot).* = psx.conv(u8, i);
            }
        }
        const k = v.g.vs_battle_secondColumnGroupCount().*;
        psx.at(v.g.vs_battle_secondColumnGroupFirstTargets(), k).* = psx.conv(u8, first -% 1);
        psx.at(v.g.vs_battle_secondColumnGroups(), k).* = psx.conv(u8, group);
        v.g.vs_battle_secondColumnGroupCount().* +%= 1;
    } else {
        while (i < v.g.vs_battle_targetSphere().*.?.unk2C) : (i +%= 1) {
            const elem = psx.at(v.g.vs_battle_targetSphere().*.?.unk40.?, i);
            if (@as(c_int, elem.unk9) == group) {
                if (first == 0) first = i +% 1;
                const row: [*]u8 = &v.g.vs_battle_targetColumnEntries().*[0];
                const slot = v.g.vs_battle_firstColumnEntryCount().*;
                v.g.vs_battle_firstColumnEntryCount().* +%= 1;
                psx.at(row, slot).* = psx.conv(u8, i);
            }
        }
        const k = v.g.vs_battle_firstColumnGroupCount().*;
        psx.at(v.g.vs_battle_firstColumnGroupFirstTargets(), k).* = psx.conv(u8, first -% 1);
        psx.at(v.g.vs_battle_firstColumnGroups(), k).* = psx.conv(u8, group);
        v.g.vs_battle_firstColumnGroupCount().* +%= 1;
    }
}
