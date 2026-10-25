//! Records the active-list indices of one actor group (src/BATTLE/BATTLE.PRG/5BF94.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BF94");

/// orig: func_800C5360 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Appends the index of every actor in `group` to one of the two index lists, and records the group's first index and id.
pub fn func_800C5360(group: c_int) callconv(.c) void {
    var first: c_int = 0;
    var i: c_int = 0;
    if (v.g.D_800F4CB1().* > v.g.D_800F4CB0().*) {
        while (i < v.g.D_800EB9B8().*.?.unk2C) : (i +%= 1) {
            const elem = psx.at(psx.get(v.g.D_800EB9B8().*.?.unk40).?, i);
            if (@as(c_int, elem.unk9) == group) {
                if (first == 0) first = i +% 1;
                const row: [*]u8 = &v.g.D_800F4C70().*[1];
                const slot = v.g.D_800F4CB0().*;
                v.g.D_800F4CB0().* +%= 1;
                psx.at(row, slot).* = psx.conv(u8, i);
            }
        }
        const k = v.g.D_800F4CB2().*;
        psx.at(v.g.D_800F4CA0(), k).* = psx.conv(u8, first -% 1);
        psx.at(v.g.D_800F4C90(), k).* = psx.conv(u8, group);
        v.g.D_800F4CB2().* +%= 1;
    } else {
        while (i < v.g.D_800EB9B8().*.?.unk2C) : (i +%= 1) {
            const elem = psx.at(psx.get(v.g.D_800EB9B8().*.?.unk40).?, i);
            if (@as(c_int, elem.unk9) == group) {
                if (first == 0) first = i +% 1;
                const row: [*]u8 = &v.g.D_800F4C70().*[0];
                const slot = v.g.D_800F4CB1().*;
                v.g.D_800F4CB1().* +%= 1;
                psx.at(row, slot).* = psx.conv(u8, i);
            }
        }
        const k = v.g.D_800F4CB3().*;
        psx.at(v.g.D_800F4CA8(), k).* = psx.conv(u8, first -% 1);
        psx.at(v.g.D_800F4C98(), k).* = psx.conv(u8, group);
        v.g.D_800F4CB3().* +%= 1;
    }
}
