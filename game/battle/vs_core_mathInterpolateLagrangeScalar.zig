//! Cubic blend of four values by the fraction arg4, stored through arg5
//! (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c).
/// orig: vs_core_mathInterpolateLagrangeScalar (pc/src/game/BATTLE/BATTLE.PRG/5BF94.c)
/// Blends arg0..arg3 with four cubic weights of t = arg4 (4096 = 1.0) and stores the result in *arg5.
pub fn vs_core_mathInterpolateLagrangeScalar(arg0: c_int, arg1: c_int, arg2: c_int, arg3: c_int, arg4: c_int, arg5: [*c]c_int) callconv(.c) void {
    const p0: c_int = ((((-%arg4) *% (arg4 -% 4096)) >> 12) *% (arg4 -% 8192));
    const p1: c_int = ((((arg4 +% 4096) *% (arg4 -% 4096)) >> 12) *% (arg4 -% 8192));
    const p2: c_int = ((((arg4 +% 4096) *% (-%arg4)) >> 12) *% (arg4 -% 8192));
    const p3: c_int = ((((arg4 +% 4096) *% arg4) >> 12) *% (arg4 -% 4096));

    const sum: c_int = arg0 *% ((((p0 >> 12) *% 2730) >> 14))
        +% arg1 *% (p1 >> 13)
        +% arg2 *% (p2 >> 13)
        +% arg3 *% ((((p3 >> 12) *% 2730) >> 14));

    arg5.* = sum >> 12;
}
