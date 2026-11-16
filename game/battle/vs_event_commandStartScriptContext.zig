//! Stores a script's return pointer in vs_event_threadCursors and returns the guest address of a
//! jump target read from the script (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");

/// orig: vs_event_commandStartScriptContext (pc/src/game/BATTLE/BATTLE.PRG/4A0A8.c)
/// Records the script's continuation (arg0 + 4) in vs_event_threadCursors[arg0[1]] and returns the address of the target at arg0 + the short at arg0 + 2.
pub fn vs_event_commandStartScriptContext(arg0: [*c]u8, _: c_short) callconv(.c) c_int {
    // Pointer arithmetic is done on integers so that a null base wraps as it does in C.
    const base: usize = @intFromPtr(arg0);
    const next: ?*u8 = @ptrFromInt(base +% 4);
    v.g.vs_event_threadCursors()[arg0[1]] = next;

    const offset_ptr: [*c]u8 = @ptrFromInt(base +% 2);
    const off: c_short = c.vs_battle_getShort(offset_ptr);
    const target: usize = base +% @as(usize, @bitCast(@as(isize, off)));
    return @bitCast(psx.addr(@as([*c]u8, @ptrFromInt(target))));
}
