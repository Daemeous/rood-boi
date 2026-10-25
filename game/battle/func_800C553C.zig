//! Clears the four score counters, places the battle groups by how many enemies each holds, and sets the layout spacing (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800C553C (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Places each group by its enemy count, largest sizes first, then sets the spacing from the highest placed index.
pub fn func_800C553C() callconv(.c) void {
    v.g.D_800F4CB0().* = 0;
    v.g.D_800F4CB1().* = 0;
    v.g.D_800F4CB2().* = 0;
    v.g.D_800F4CB3().* = 0;
    var size: c_int = 6;
    while (size > 0) : (size -%= 1) {
        var group: c_int = 0;
        while (group < 16) : (group +%= 1) {
            var count: c_int = 0;
            var i: c_int = 0;
            while (i < @as(c_int, v.g.D_800EB9B8().*.?.unk2C)) : (i +%= 1) {
                const entry = psx.at(psx.get(v.g.D_800EB9B8().*.?.unk40).?, i);
                if (@as(c_int, entry.unk9) == group) count +%= 1;
            }
            if (count == size) c.func_800C5360(group);
        }
    }
    var i: c_int = 0;
    while (i < @as(c_int, v.g.D_800EB9B8().*.?.unk2C)) : (i +%= 1) {
        const group: c_int = @as(c_int, psx.at(psx.get(v.g.D_800EB9B8().*.?.unk40).?, i).unk9);
        if ((group >> 4) != 0) c.func_800C5360(group);
    }
    var lo: c_int = @as(c_int, v.g.D_800F4CB1().*);
    if (lo < @as(c_int, v.g.D_800F4CB0().*)) lo = @as(c_int, v.g.D_800F4CB0().*);
    v.g.D_800F4CB4().* = psx.conv(u8, ((16 -% lo) *% 11) >> 1);
}
