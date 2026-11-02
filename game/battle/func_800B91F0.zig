//! Reads a 16-bit value at offset 1 of a script operand and passes it on with
//! the byte at offset 3 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const c = @import("c_4A0A8");

/// orig: func_800B91F0 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Looks up the short at offset 1 and calls func_8009DBDC with it and the byte at offset 3.
pub fn func_800B91F0(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const value = c.vs_battle_getShort(arg0 + 1);
    // The callee is called through a cast, as the C calls it: its prototype is not in the unit's declarations.
    const looked = @as(*const fn (c_short) callconv(.c) c_int, @ptrCast(&c.func_800BFE50))(value);
    @as(*const fn (c_int, c_int) callconv(.c) void, @ptrCast(&c.func_8009DBDC))(looked, @as(c_int, arg0[3]));
    return 0;
}
