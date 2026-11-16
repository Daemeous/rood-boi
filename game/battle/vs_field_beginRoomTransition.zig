//! Starts a screen transition into a door's room and loads its map (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_beginRoomTransition (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Copies a room door section into the transition state with `effect` and `speed`, then loads its map.
pub fn vs_field_beginRoomTransition(door: ?*v._mpdRoomDoorSection_t, effect: c_int, speed: c_int) callconv(.c) c_int {
    v.g.vs_field_pendingDestinationDoor().* = door.?.*;
    v.g.vs_battle_screenTransitionStep().* = 1;
    v.g.vs_battle_screenTransitionEffect().* = effect;
    v.g.vs_battle_screenTransitionSpeed().* = speed;
    v.g.vs_field_pendingDestinationDoor().zndId = psx.conv(u8, v.g._zoneContext().zndId);
    c._loadMpd(@as(c_int, door.?.mpdId));
    c.func_8009E5C4(0);
    c.vs_field_setActorCollisionBypass(0, 1);
    c.vs_field_setEntranceDoorRecord(null);

    return 1;
}
