//! Room door section: runs the door setup, then passes three packed result bytes on to vs_field_findEntranceDoorNearTile (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_beginScriptedRoomTransition (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Runs the room door setup for a section, then passes three packed result bytes to vs_field_findEntranceDoorNearTile.
pub fn vs_field_beginScriptedRoomTransition(arg0: ?*v._mpdRoomDoorSection_t, arg1: c_int, arg2: c_int) callconv(.c) void {
    var sp10: v.func_8006EBF8_t = undefined;
    _ = c.vs_field_beginRoomTransition(@ptrCast(arg0), arg1, arg2);
    _ = c.vs_actors_actorPlacement(0, &sp10);
    const fields = &sp10.location.packedTile.fields;
    c.vs_field_findEntranceDoorNearTile(
        @as(c_int, psx.getBits(fields, "unk0_0")),
        @as(c_int, psx.getBits(fields, "unk0_16")),
        @as(c_int, psx.getBits(fields, "unk0_8")),
    );
}
