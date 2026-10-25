//! Clears the state of one entry of the D_800F4538 table (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: func_8009DC38 (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Releases the D_800F4538 entry `arg0` if present: calls func_8009DC8C, then clears its flags and timer words.
pub fn func_8009DC38(arg0: c_int) callconv(.c) void {
    const temp_s0 = psx.at(v.g.D_800F4538(), arg0).*;
    if (temp_s0) |t| {
        c.func_8009DC8C(arg0);
        t.unk0.unk1A = 0;
        t.unk0.unk18 = 0;
        @as(*align(1) c_int, @ptrCast(&t.unk0.unk34)).* = 0;
    }
}
