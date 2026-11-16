//! Byte lookup in the table vs_event_opcodeSizes (pc/src/game/BATTLE/BATTLE.PRG/573B8.c).
const v = @import("views_573B8");

/// orig: vs_event_opcodeSize (pc/src/game/BATTLE/BATTLE.PRG/573B8.c)
/// Returns the byte of vs_event_opcodeSizes selected by the byte at arg0.
pub fn vs_event_opcodeSize(arg0: [*c]u8) callconv(.c) c_int {
    const table = v.g.vs_event_opcodeSizes();
    return @as(c_int, table[arg0[0]]);
}
