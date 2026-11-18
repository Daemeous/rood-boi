//! Battle teardown: clears the two frame buffers' order tables and transitions,
//! then frees the zone's enemy list and the battle's heap blocks (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_unloadCurrentRoom (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Clears and transitions both frame buffers, then tears down the battle's heap and scene state.
pub fn vs_field_unloadCurrentRoom() callconv(.c) void {
    var i: c_int = 0;
    while (i < 2) : (i += 1) {
        const frame: ?*anyopaque = psx.at(v.g.vs_render_legacyOrderingTables(), v.g.vs_main_frameBuf().*).*;
        const frame_addr: usize = @intFromPtr(frame);
        psx.put(&v.g.vs_scratch().unk4, @as(?*anyopaque, @ptrFromInt(frame_addr +% 0x10)));

        // The scratch slot holds a guest address; it is passed on as the C's pointer value.
        const slot = psx.get(v.g.vs_scratch().unk4);
        const slot_addr: usize = @intFromPtr(slot);
        c.pc_frame_table_clear(@ptrFromInt(slot_addr), 0x800, 1);
        _ = c.func_8007DF40();
        c._drawScreenTransition(@ptrFromInt(slot_addr));

        _ = c.vs_render_presentFieldFrame(@ptrFromInt(slot_addr +% 0x1FFC));
    }

    _ = c.DrawSync(0);
    _ = c.vs_main_gametimeUpdate(0);
    c.vs_event_freeRoomEventSections();
    c._setRoomSeen();
    _ = c.vs_field_releaseOwnedRoomSections(0);

    if (psx.get(v.g._zoneContext().mpdEnemies)) |mpd| {
        c.vs_main_freeHeap(mpd);
    }

    if (v.g.D_800F192C().*) |actor| {
        c.vs_battle_aiFreeActorAiState(@ptrCast(actor));
    }
    c.vs_field_removeRoomActors();
    c.vs_battle_aiFreeRoomAiState();
    c.vs_battle_setActorMarker(0, 0);
    c.vs_actors_resetAllActorMovement();
    c.vs_items_clearRoomLootList();
    c.vs_main_freeHeap(v.g.vs_render_legacyPrimitiveBuffers()[1]);
    c.vs_main_freeHeap(v.g.vs_render_legacyPrimitiveBuffers()[0]);
}
