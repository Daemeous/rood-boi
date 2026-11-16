//! Builds a weapon's object data slot and hands it to the battle data-slot populator
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_queueEquipmentModelLoad (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Fills an object data slot for an actor's weapon (or shield, when isShield is set) and populates it.
pub fn vs_actors_queueEquipmentModelLoad(actor: ?*v.vs_battle_actor, actorId: c_int, wepId: c_int, isShield: c_int, material: c_int) callconv(.c) void {
    if (wepId == 0xFF) return;

    const tempS0: c_int = actorId *% 2 +% isShield;
    _ = c.vs_content_releaseWeaponModel(tempS0);
    if (wepId == 0) return;

    // The C leaves the unset fields of sp10 uninitialised.
    var sp10: v.vs_battle_objectData = undefined;
    sp10.dataType = 3;
    sp10.index = psx.conv(i8, tempS0);
    sp10.modelId = psx.conv(i16, wepId);
    psx.put(&sp10.destination, psx.get(psx.at(&actor.?.unk48, isShield).*));
    sp10.actorId = psx.conv(u8, actorId);
    if (isShield == 0) {
        sp10.unk11 = 0xF0;
    } else {
        sp10.unk11 = 0xF1;
    }
    sp10.material = psx.conv(u8, material);
    _ = c.vs_battle_populateDataSlot(@ptrCast(&sp10));
}
