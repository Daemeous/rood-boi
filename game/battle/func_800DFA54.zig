//! Looks up a group's table entry for a position and reports its first entry's fields
//! (src/BATTLE/BATTLE.PRG/771A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_771A8");

/// orig: func_800DFA54 (src/BATTLE/BATTLE.PRG/771A8.c)
/// Finds the entry of group `group` matching (x, y) at 1/512 scale, else takes the group's first entry, and writes its fields to `out`.
pub fn func_800DFA54(group: c_int, x: c_int, y: c_int, out: ?*v.func_800DFA54_t) callconv(.c) void {
    const table = v.g.D_800F5910().*.?;
    const x_key: c_int = x >> 19;
    const y_key: c_int = y >> 19;

    var i: c_int = group *% 2;
    const end: c_int = group *% 2 +% 2;
    found: {
        while (i < end) : (i +%= 1) {
            const e = psx.at(table, i *% 2 +% 1);
            if (@as(c_int, psx.getBits(e, "unk0_0")) == x_key and
                @as(c_int, psx.getBits(e, "unk0_5")) == y_key)
            {
                break :found;
            }
        }
        i = group *% 2;
    }

    const first = psx.at(table, i *% 2);
    const o = out.?;
    o.unk0 = psx.conv(u8, psx.getBits(first, "unk0_0"));
    o.unk2 = psx.conv(u8, psx.getBits(first, "unk0_5"));
}
