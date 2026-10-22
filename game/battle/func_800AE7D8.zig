//! Pans two sound effects (0x7E) at a position offset 0x1C into arg0's block
//! (src/BATTLE/BATTLE.PRG/44F14.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_44F14");

/// orig: func_800AE7D8 (src/BATTLE/BATTLE.PRG/44F14.c)
/// Pans sound effect 0x7E at the vector 0x1C into `block` for `arg1`, and again for `arg2` when it is nonzero.
pub fn func_800AE7D8(arg0: ?*anyopaque, arg1: c_int, arg2: c_int) callconv(.c) void {
    // temp_s0 = arg0 + 0x1C: a byte offset from a void pointer (a host address).
    const temp_s0: [*c]c.SVECTOR = @ptrFromInt(@intFromPtr(arg0) +% 0x1C);

    c.vs_main_panSfx(0x7E, arg1, temp_s0);
    if (arg2 != 0) {
        c.vs_main_panSfx(0x7E, arg2, temp_s0);
    }
}
