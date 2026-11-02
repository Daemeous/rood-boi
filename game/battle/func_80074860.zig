//! Whether the room's event script holds a type-4 event for the cell an
//! actor stands on (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: func_80074860 (src/BATTLE/BATTLE.PRG/146C.c)
/// Finds actor `arg0`'s cell (func_800A1108, func_80074798) and asks the event script (func_800BEC58, type 4, mode 1) for an event there: 1 when it answers 1.
pub fn func_80074860(arg0: c_int) callconv(.c) c_int {
    // uninitialised in the original until func_800A1108 and func_80074798 fill them
    var sp10: v.func_8006EBF8_t = undefined;
    var stack: [4]u8 = undefined;
    // ROOD_PC_LP64: the cell's address is passed as an int, so the C makes
    // the buffer its block-scope static (globalised by the build); a stack
    // buffer on 32-bit, where the static does not exist.
    const sp20: *[4]u8 = if (comptime @sizeOf(usize) == 8) v.g.func_80074860__sp20() else &stack;

    _ = c.func_800A1108(arg0, @ptrCast(&sp10));
    _ = c.func_80074798(@ptrCast(&sp10), &sp20[0]);
    // The C's "BUG: arg2 should be an actor ID?": in mode 1 func_800BEC58
    // reads the cell's bytes [0] and [2] through it, so the address is meant.
    return @intFromBool(c.func_800BEC58(4, 0, @bitCast(psx.addr(sp20)), 1) == 1);
}
