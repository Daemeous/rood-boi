//! Script jump forward: the target of a relative forward jump in a battle script
//! (src/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const c = @import("c_4A0A8");

/// orig: vs_battle_script_jumpFwd (src/BATTLE/BATTLE.PRG/4A0A8.c)
/// Returns the script address a forward jump at `arg0` lands on: its 16-bit operand added to the script pointer.
pub fn script_jumpFwd(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // arg0 + 1 is the operand; computed as an address so a null base wraps as in C.
    const operand_ptr: [*c]u8 = @ptrFromInt(@intFromPtr(arg0) +% 1);
    const off = c.vs_battle_getShort(operand_ptr);
    // The index is a short, sign-extended and added with wrapping; only the low 32 bits
    // survive the (long) cast and the int return, so 32-bit wrapping arithmetic matches.
    const target = psx.conv(u32, @intFromPtr(arg0)) +% @as(u32, @bitCast(@as(i32, off)));
    return @bitCast(target);
}
