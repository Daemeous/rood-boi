//! Sets bit 0x1000 in the word func_8009195C returns for arg0 (src/BATTLE/BATTLE.PRG/2842C.c).
const c = @import("c_2842C");

/// orig: func_80091AC0 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Flags the word func_8009195C finds for `arg0` with 0x1000; returns 1 if there was one, else 0.
pub fn func_80091AC0(arg0: c_int) callconv(.c) c_int {
    const found = c.func_8009195C(arg0);
    if (found) |p| {
        // The C's int* comes back as an untyped pointer; access it as an unaligned word, as x86 does.
        const word: *align(1) c_int = @ptrCast(p);
        word.* |= 0x1000;
        return 1;
    }
    return 0;
}
