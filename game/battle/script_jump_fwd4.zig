//! Script jump target: a 16-bit forward offset read after the opcode
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");
const event_jump = @import("event_jump.zig");

/// orig: vs_battle_script_jumpFwd4 (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns the script address a jump opcode at `arg0` lands on: `arg0` plus the signed short at `arg0 + 1`.
pub fn script_jumpFwd4(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // The C is `&arg0[getShort(arg0 + 1)]`. Done in integers, so a null or out-of-range
    // base wraps as C's pointer arithmetic does, without a Zig trap.
    const next: [*c]u8 = psx.at(arg0, 1);
    const off: c_short = c.vs_battle_getShort(next);
    return event_jump.to(psx.at(arg0, off));
}
