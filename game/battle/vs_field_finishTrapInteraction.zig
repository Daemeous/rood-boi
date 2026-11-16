//! Resets the camera mode and game tick speed, then runs two battle setup calls on the entry at vs_field_interactionFocus (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_finishTrapInteraction (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the camera mode and tick speed, then calls two setup functions on the room entry's sections.
pub fn vs_field_finishTrapInteraction() callconv(.c) void {
    v.g._cameraMode().* = 4;
    v.g.vs_gametime_tickspeed().* = 2;
    c.func_800CB50C();
    c.vs_field_setRoomTriggerEnabled(psx.cptr(c._mpdRoomSection9, &psx.get(v.g.vs_field_interactionFocus().unk0).?.trigger), 1);
    c.vs_battle_aiMarkAiTileDynamicObstacle(
        @as(c_int, psx.get(v.g.vs_field_interactionFocus().unk0).?.trigger.tileX),
        @as(c_int, psx.get(v.g.vs_field_interactionFocus().unk0).?.trigger.tileZ),
    );
}
