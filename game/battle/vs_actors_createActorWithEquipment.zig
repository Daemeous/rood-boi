//! Spawns an object from its parts and waits until the object-data queue has room
//! (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_createActorWithEquipment (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Creates an object data actor, drains the object-data queue until a slot is free, then finishes its setup.
pub fn vs_actors_createActorWithEquipment(arg0: c_int, arg1: c_int, bladeWepId: c_int, bladeMaterial: c_int, shieldWepId: c_int, shieldMaterial: c_int, arg6: ?*v.vs_battle_objectData_flags, arg7: c_int) callconv(.c) ?*v.vs_battle_actor {
    const raw: ?*anyopaque = @ptrCast(c.vs_actors_allocateActorWithEquipment(arg0, arg1, bladeWepId, bladeMaterial, shieldWepId, shieldMaterial, @ptrCast(arg6), arg7));
    const aligned: ?*align(4) anyopaque = @alignCast(raw);
    const temp_v0: ?*v.vs_battle_actor = @ptrCast(@alignCast(aligned));
    if (temp_v0) |actor| {
        while (c.vs_battle_getEmptyObjectDataSlot() != 0) {
            _ = c.vs_battle_processObjectDataQueue();
            _ = c.vs_main_gametimeUpdate(0);
        }
        c.vs_actors_registerLoadedActor(@ptrCast(actor), arg0, arg1, bladeWepId, shieldWepId, arg7);
        return actor;
    }
    return null;
}
