//! Sets up the battle room section, the camera mode, the per-slot state and the
//! opening scene message (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_beginTrapInteraction (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Starts a battle on room section `section` with source pose `source` and `arg2`, and shows the opening message when the information setting is on.
pub fn vs_field_beginTrapInteraction(section: ?*v._mpdRoomSectionA, source: ?*v.vs_actors_ActorLocation, arg2: c_int) callconv(.c) void {
    v.g.D_800F196C().* = 8;
    v.g._cameraMode().* = 1;
    _ = c.vs_field_storePreferredActionCameraDistance();
    psx.put(&v.g.vs_field_interactionFocus().unk0, section);
    v.g.vs_field_interactionFocus().unk4.location = source.?.*;
    v.g.vs_field_interactionFocus().unk4.actorIndex = arg2;
    for (0..16) |i| {
        c.func_8009E5C4(@intCast(i));
    }
    if (v.g.vs_main_settings().information != 0) {
        c.vs_battle_displaySceneMessage(0xB, @as(c_int, section.?.trigger.unk6) +% 6, 1);
    }
    v.g.vs_battle_actionState().*.?.unk4 = 0;
    v.g.vs_battle_actionState().*.?.unk8.actionIndex = 0;
}
