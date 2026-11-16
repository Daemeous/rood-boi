//! Camera mode and the field/battle transition for the battle overlay (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_field_dispatchTrapOrChestInteraction (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Sets the camera mode to 2 and runs the transition for the current scene, at a faster tick speed when the scene state is 8.
pub fn vs_field_dispatchTrapOrChestInteraction() callconv(.c) void {
    v.g._cameraMode().* = 2;
    if (v.g.D_800F196C().* == 8) {
        v.g.vs_gametime_tickspeed().* = 4;
        c.vs_field_triggerActorTrap(@ptrCast(psx.get(v.g.vs_field_interactionFocus().unk0)), @ptrCast(&v.g.vs_field_interactionFocus().unk4), v.g.vs_field_interactionFocus().unk4.actorIndex);
        return;
    }
    c.vs_field_tryOpenChest(@ptrCast(psx.get(v.g.vs_field_interactionFocus().unk0)), v.g.vs_field_interactionFocus().unk4.actorIndex);
}
