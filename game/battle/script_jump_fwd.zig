//! Script jump forward: the target of a relative forward jump in a battle script
//! (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");
const event_jump = @import("event_jump.zig");

/// orig: vs_battle_script_jumpFwd (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns the script address a forward jump at `arg0` lands on: its 16-bit operand added to the script pointer.
pub fn script_jumpFwd(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // arg0 + 1 is the operand; computed as an address so a null base wraps as in C.
    const operand_ptr: [*c]u8 = psx.at(arg0, 1);
    const off = c.vs_battle_getShort(operand_ptr);
    // The index is a short, sign-extended and added with wrapping.
    return event_jump.to(psx.at(arg0, off));
}
