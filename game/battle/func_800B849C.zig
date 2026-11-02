//! An event script opcode that hands func_8009FBD8 an actor and a table-based value (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B849C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_8009FBD8 with the actor of getShort(script + 1) and D_800E9B58[script[3]] +
/// script[4], or -1 when script[4] is 0xFF.
pub fn func_800B849C(script: [*c]u8, _: c_short) callconv(.c) c_int {
    const actor: c_int = c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1)));
    if (script[4] == 0xFF) {
        _ = c.func_8009FBD8(actor, -1);
    } else {
        // D_800E9B58 is indexed unchecked by script[3], as in the C.
        _ = c.func_8009FBD8(actor, @as(c_int, psx.at(v.g.D_800E9B58(), script[3]).*) + script[4]);
    }
    return 0;
}
