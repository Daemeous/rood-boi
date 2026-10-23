//! Adds two packed 16-bit lanes of a pair of words, each lane wrapping on its own
//! (src/BATTLE/BATTLE.PRG/2842C.c).
/// orig: func_80096254 (src/BATTLE/BATTLE.PRG/2842C.c)
/// Adds the low halfwords and the high halfwords of its two arguments separately, without a carry between them.
pub fn func_80096254(arg0: c_int, arg1: c_int) callconv(.c) c_int {
    const a: u32 = @bitCast(arg0);
    const b: u32 = @bitCast(arg1);

    // Low lane: unsigned 16-bit wrap.
    const lo: u32 = ((a & 0xFFFF) +% (b & 0xFFFF)) & 0xFFFF;

    // High lane: C's signed `>> 16` is arithmetic; the sum wraps as int.
    const hi_a: i32 = arg0 >> 16;
    const hi_b: i32 = arg1 >> 16;
    const sum: i32 = hi_a +% hi_b;
    const hi: u32 = @as(u32, @bitCast(sum)) << 16;

    return @bitCast(lo | hi);
}
