//! Swaps the 32-bit words behind two slot lookups (src/BATTLE/BATTLE.PRG/2842C.c).
const c = @import("c_2842C");

/// orig: func_80091A1C (src/BATTLE/BATTLE.PRG/2842C.c)
/// Swaps the words at two looked-up slots; returns 1 if both lookups found one, else 0.
pub fn func_80091A1C(arg0: c_int, arg1: c_int) callconv(.c) c_int {
    // The lookup returns an untyped pointer; the C reads it as `int*`. Keep the
    // alignment unchecked (x86 does not trap on a misaligned word, the C does not either).
    const s0: ?*align(1) c_int = @ptrCast(c.func_8009195C(arg0));
    const a1: ?*align(1) c_int = @ptrCast(c.func_8009195C(arg1));

    if (s0 != null and a1 != null) {
        const p0 = s0.?;
        const p1 = a1.?;
        const t = p0.*;
        p0.* = p1.*;
        p1.* = t;
        return 1;
    }
    return 0;
}
