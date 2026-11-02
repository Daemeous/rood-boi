//! Dispatches three 2-bit option fields of byte 1 of a script to C helpers (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B9B18 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls one helper per 2-bit field of byte 1 of `arg0` (bits 0-1, 2-3, 4-5); returns 0.
pub fn func_800B9B18(arg0: [*c]u8, arg1: c_short) callconv(.c) c_int {
    _ = arg1;
    const flags: c_int = @as(c_int, arg0[1]);

    switch (flags & 3) {
        0 => c.func_8007C424(),
        1 => c.func_8007C404(),
        else => {},
    }

    switch ((flags >> 2) & 3) {
        0 => c.func_8007BF4C(),
        1 => c.func_8007BF2C(),
        else => {},
    }

    switch ((flags >> 4) & 3) {
        0 => c.func_8007C050(),
        1 => c.func_8007BFC0(),
        else => {},
    }

    return 0;
}
