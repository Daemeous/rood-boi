//! Calls func_800D46DC with whether the frame's flagged record is idle, then returns 1
//! (src/BATTLE/BATTLE.PRG/5BF94.c).
const psx = @import("common").psx;
const v = @import("views_5BF94");
const c = @import("c_5BF94");

/// orig: func_800D4E90 (src/BATTLE/BATTLE.PRG/5BF94.c)
/// Passes whether the record's unk30 target has unk4 == 0 to func_800D46DC; always returns 1.
pub fn func_800D4E90(arg0: ?*v.D_800F53B8_t) callconv(.c) c_int {
    const record = arg0.?;
    c.func_800D46DC(@intFromBool(psx.get(record.unkD1C.unk30).?.unk4 == 0), @ptrCast(arg0));
    return 1;
}
