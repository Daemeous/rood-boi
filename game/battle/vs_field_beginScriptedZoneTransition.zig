//! Runs the room door section update and reports its three door fields (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_beginScriptedZoneTransition (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Updates a room door section, then passes three bitfields of its result to vs_field_findEntranceDoorNearTile.
pub fn vs_field_beginScriptedZoneTransition(section: ?*v._mpdRoomDoorSection_t, arg1: c_int, arg2: c_int) callconv(.c) void {
    var sp10: v.func_8006EBF8_t = undefined;

    _ = c.vs_field_beginZoneTransition(@ptrCast(section), arg1, arg2);
    _ = c.vs_actors_actorPlacement(0, &sp10);
    c.vs_field_findEntranceDoorNearTile(
        @as(c_int, psx.getBits(&sp10.unk0.unk0.fields, "unk0_0")),
        @as(c_int, psx.getBits(&sp10.unk0.unk0.fields, "unk0_16")),
        @as(c_int, psx.getBits(&sp10.unk0.unk0.fields, "unk0_8")),
    );
}
