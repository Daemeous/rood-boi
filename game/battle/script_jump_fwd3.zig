//! Script jump operand: the address a forward jump opcode lands on (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");
const event_jump = @import("event_jump.zig");

/// orig: vs_battle_script_jumpFwd3 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns the script address `arg0` plus the signed 16-bit operand at `arg0 + 1`, truncated to 32 bits.
pub fn script_jumpFwd3(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    const off: c_short = c.vs_battle_getShort(arg0 + 1);
    return event_jump.to(psx.at(arg0, off));
}
