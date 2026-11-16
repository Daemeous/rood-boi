//! Whether the room's event script holds a type-4 event for the cell an
//! actor stands on (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_tryTriggerFacingTileEvent (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Finds actor `arg0`'s cell (vs_actors_actorPlacement, vs_field_tileInFrontOfActor) and asks the event script (vs_event_evaluateRoomEventTriggers, type 4, mode 1) for an event there: 1 when it answers 1.
pub fn vs_field_tryTriggerFacingTileEvent(arg0: c_int) callconv(.c) c_int {
    // uninitialised in the original until vs_actors_actorPlacement and vs_field_tileInFrontOfActor fill them
    var sp10: v.func_8006EBF8_t = undefined;
    var stack: [4]u8 = undefined;
    // ROOD_PC_LP64: the cell's address is passed as an int, so the C makes
    // the buffer its block-scope static (globalised by the build); a stack
    // buffer on 32-bit, where the static does not exist.
    const sp20: *[4]u8 = if (comptime @sizeOf(usize) == 8) v.g.func_80074860__sp20() else &stack;

    _ = c.vs_actors_actorPlacement(arg0, @ptrCast(&sp10));
    _ = c.vs_field_tileInFrontOfActor(@ptrCast(&sp10), &sp20[0]);
    // The C's "BUG: arg2 should be an actor ID?": in mode 1 vs_event_evaluateRoomEventTriggers
    // reads the cell's bytes [0] and [2] through it, so the address is meant.
    return @intFromBool(c.vs_event_evaluateRoomEventTriggers(4, 0, @bitCast(psx.addr(sp20)), 1) == 1);
}
