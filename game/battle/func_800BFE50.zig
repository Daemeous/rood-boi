//! Battle helper from src/BATTLE/BATTLE.PRG/573B8.c: decodes a 16-bit code by its top nibble.
const c = @import("c_573B8");

/// orig: func_800BFE50 (src/BATTLE/BATTLE.PRG/573B8.c)
/// Top nibble 0 calls func_8007CF18 on the low 12 bits, 1 keeps the low 5 bits, 2 returns the code.
pub fn func_800BFE50(arg0: c_ushort) callconv(.c) c_int {
    // Nibbles 3..15 have no case and no return in the C (undefined behaviour). The PS1
    // object returns arg0 for them too: case 2's test `beq v1, 2` has `v0 = arg0` in its
    // delay slot, which runs whichever way the branch goes. Every caller passes 0..2.
    switch (arg0 >> 12) {
        0 => return c.func_8007CF18(@as(c_int, arg0 & 0xFFF)),
        1 => return @as(c_int, arg0 & 0x1F),
        else => return @as(c_int, arg0),
    }
}
