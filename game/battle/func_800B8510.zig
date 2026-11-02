//! An event script opcode that points an actor at another actor with mode -2 (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const c = @import("c_4A0A8");

/// orig: func_800B8510 (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8009FC60 for the actor of getShort(script + 1) with the actor of
/// getShort(script + 3) (0x2000 as -2), no vector and script[5] (0 as 0x1000).
pub fn func_800B8510(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const target: c_int = c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 3)));
    const actor: c_int = c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1)));
    var other: c_int = -2;
    if (target != 0x2000) other = target;
    var speed: c_int = 0x1000;
    if (script[5] != 0) speed = script[5];
    _ = c.func_8009FC60(actor, other, null, speed);
    return 0;
}
