//! An event script opcode that sets or clears an actor's state (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: func_800B9608 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// For the actor of getShort(script + 1): func_8007CAA4 when script[3] is 0xFE, else
/// func_8007CA20 with script[3] and script[5].
pub fn func_800B9608(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const actor: u16 = psx.conv(u16, c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    if (script[3] == 0xFE) {
        c.func_8007CAA4(actor);
    } else {
        c.func_8007CA20(actor, script[3], script[5]);
    }
    return 0;
}
