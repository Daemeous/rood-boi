//! Drops empty entries from the battle status-text queue (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

/// orig: vs_battle_compactFloatingLabels (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// `count` times: removes the first queue entry whose kind (unk8.u8[0]) is 0.
pub fn vs_battle_compactFloatingLabels(count: c_int) callconv(.c) void {
    var i: c_int = 0;
    while (i < count) : (i +%= 1) {
        var j: c_int = 0;
        while (j < @as(c_int, v.g.vs_battle_floatingLabelCount().*)) : (j +%= 1) {
            const queue: [*c]v.D_800F1BAC_t = @ptrCast(@alignCast(v.g.vs_battle_floatingLabels().*));
            if (psx.at(queue, j).unk8.u8[0] == 0) {
                c.vs_battle_removeFloatingLabel(j);
                break;
            }
        }
    }
}
