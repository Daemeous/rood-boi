//! Forwards a pointer 0x2084 bytes into the block at vs_render_legacyCurrentOrderingTable to vs_render_splicePrimitiveList (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_render_appendOverlayPrimitive (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Calls vs_render_splicePrimitiveList with the address 0x2084 bytes past vs_render_legacyCurrentOrderingTable and the given pointer.
pub fn vs_render_appendOverlayPrimitive(arg0: [*c]c_int) callconv(.c) void {
    const base: usize = @intFromPtr(v.g.vs_render_legacyCurrentOrderingTable().*);
    const p: [*c]c_uint = @ptrFromInt(base +% 0x2084);
    // ROOD_PC: a copy (DR_MOVE) becomes a draw item (pc/src/draw_items.c); the marker is spliced where the packet was.
    c.pc_frame_copy_tail(p, @ptrCast(arg0));
}
