//! Walks an array of 24-byte entries and dispatches each entry with a zero high
//! nibble in its type byte (src/BATTLE/BATTLE.PRG/5BE50.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_5BE50");
const c = @import("c_5BE50");

/// orig: func_800C4650 (src/BATTLE/BATTLE.PRG/5BE50.c)
/// Runs the entries of an array whose type byte has a zero high nibble, through one of two handlers chosen by a lookup.
pub fn func_800C4650(arg0: ?*v.func_800C4650_t, arg1: c_int) callconv(.c) void {
    var i: c_int = 0;
    while (i < arg1) : (i += 1) {
        // C advances arg0 once per iteration; indexing the array is the same address.
        const entry: *v.func_800C4650_t = &@as([*]v.func_800C4650_t, @ptrCast(arg0.?))[@intCast(i)];
        const kind: u8 = entry.unk9;
        if (kind >> 4 == 0) {
            const ability: *v.func_800C1564_t = &v.g.D_800EB9B8().*.?.unk0[1];
            if (c.func_800C1564(@ptrCast(ability), @ptrCast(&entry.unk0)) != 0) {
                _ = c.func_8009FD5C(@as(c_int, kind), 0, @as(c_int, entry.unkA));
            } else {
                _ = c.func_8009FE74(@as(c_int, kind), @as(c_int, entry.unkA));
            }
        }
    }
}
