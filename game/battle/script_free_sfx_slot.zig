//! Script opcode: frees the sound effect queue slot named by the script's second byte
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: vs_battle_script_freeSfxSlot (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Frees the sound effect queue slot at script byte 1; returns 1 if the free succeeded, else 0.
pub fn script_freeSfxSlot(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // arg0[1] is a u_char: it widens to u_int (zero-extended) as the C argument.
    const slot: c_uint = psx.conv(c_uint, psx.at(arg0, @as(c_int, 1)).*);
    return @as(c_int, @intFromBool(c.vs_main_freeSfxQueueSlot(slot) != 0));
}
