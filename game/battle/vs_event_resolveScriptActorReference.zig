//! Battle helper from pc/src/game/BATTLE/BATTLE.PRG/573B8.c: decodes a 16-bit code by its top nibble.
const c = @import("c_573B8");

/// orig: vs_event_resolveScriptActorReference (pc/src/game/BATTLE/BATTLE.PRG/573B8.c)
/// Top nibble 0 calls vs_actors_findActorByModelId on the low 12 bits, 1 keeps the low 5 bits, 2 returns the code.
pub fn vs_event_resolveScriptActorReference(arg0: c_ushort) callconv(.c) c_int {
    // Nibbles 3..15 have no case and no return in the C (undefined behaviour). The PS1
    // object returns arg0 for them too: case 2's test `beq v1, 2` has `v0 = arg0` in its
    // delay slot, which runs whichever way the branch goes. Every caller passes 0..2.
    switch (arg0 >> 12) {
        0 => return c.vs_actors_findActorByModelId(@as(c_int, arg0 & 0xFFF)),
        1 => return @as(c_int, arg0 & 0x1F),
        else => return @as(c_int, arg0),
    }
}
