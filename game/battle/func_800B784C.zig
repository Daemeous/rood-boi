//! An event script opcode that hands an actor four bytes of the script and two optional values (src/BATTLE/BATTLE.PRG/4A0A8.c).
const psx = @import("common").psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: func_800B784C (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Calls func_800A97EC for the actor of getShort(script + 1) with script[3..6], and 0, 0 while
/// D_800F4C2C is 2, else script[7] and script[8] (0xFF as -1); then func_8009DC8C and its D_800F4B70 mark.
pub fn func_800B784C(script: [*c]u8, _: c_short) callconv(.c) c_int {
    // The C's buffer is 10 bytes; only the first 4 are set and read.
    var bytes: [10]u8 = undefined;
    const actor: u16 = psx.conv(u16, c.func_800BFE50(psx.conv(u16, c.vs_battle_getShort(script + 1))));
    bytes[0] = script[3];
    bytes[1] = script[4];
    bytes[2] = script[5];
    bytes[3] = script[6];
    const fields: ?*c.func_8006EBF8_t_fields = @ptrCast(&bytes);
    if (v.g.D_800F4C2C().* == 2) {
        c.func_800A97EC(actor, fields, 0, 0);
    } else {
        const a: c_int = if (script[7] != 0xFF) script[7] else -1;
        const b: c_int = if (script[8] != 0xFF) script[8] else -1;
        c.func_800A97EC(actor, fields, a, b);
    }
    c.func_8009DC8C(actor);
    v.g.D_800F4B70()[actor] = 1;
    return 0;
}
