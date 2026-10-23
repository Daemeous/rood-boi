//! Clears bit 0x100 of the word `func_8009195C` finds for an index
//! (src/BATTLE/BATTLE.PRG/2842C.c).
const c = @import("c_2842C");

/// orig: func_80091998 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Clears bit 0x100 of the word `func_8009195C(arg0)` returns; 1 if there was one, else 0.
pub fn func_80091998(arg0: c_int) callconv(.c) c_int {
    const temp_v0: ?*align(1) c_int = @ptrCast(c.func_8009195C(arg0));
    if (temp_v0) |word| {
        word.* &= ~@as(c_int, 0x100);
        return 1;
    }
    return 0;
}
