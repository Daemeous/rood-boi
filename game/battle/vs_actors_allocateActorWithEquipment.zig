//! The battle actor a slot is filled with: its stats, its weapon and shield
//! slots, and its equipped category (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_actors_allocateActorWithEquipment (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Allocates and sets up the battle actor for slot `arg0`, equipping its blade and shield unless `arg1` is 0x7F.
pub fn vs_actors_allocateActorWithEquipment(arg0: c_int, arg1: c_int, bladeWepId: c_int, bladeMaterial: c_int, shieldWepId: c_int, shieldMaterial: c_int, arg6: ?*v.vs_battle_objectData_flags, arg7: c_int) callconv(.c) ?*v.vs_battle_actor_dat {
    if (psx.at(v.g.vs_battle_actors(), arg0).* != null) return null;

    if (arg0 >= 0xA) return null;

    const temp: *v.vs_battle_actor_dat = @ptrCast(@alignCast(c.vs_main_allocHeap(@sizeOf(v.vs_battle_actor_dat)).?));
    c.vs_main_bzero(temp, @sizeOf(v.vs_battle_actor_dat2_unk0));
    const actor: *v.vs_battle_actor = &temp.unk0.unk0.unk0;

    psx.put(&actor.stats, &temp.unk0.unk0.unk50);
    actor.unk40 = 1;
    psx.put(&actor.unk44, &temp.unk0.unk9B4);
    psx.put(&actor.unk48[0], &temp.unk22B4[0]);
    psx.put(&actor.unk48[1], &temp.unk22B4[1]);

    var i: c_int = 0xA;
    if (bladeWepId != 0) {
        i = 1;
        // i stays in 1..10 here: the C loop's own bound.
        while (i < 11) : (i +%= 1) {
            if (@as(c_int, v.g._wepIdCategories()[@intCast(i)]) >= bladeWepId) break;
        }
    }

    actor.equippedWeaponCategory = psx.conv(u8, i);

    _ = c.vs_actors_queueActorModelLoad(@ptrCast(actor), arg0, arg1, @ptrCast(arg6), arg7);
    if (arg1 != 0x7F) {
        c.vs_actors_queueActorBaseSequenceLoad(@ptrCast(actor), arg0);
        c.vs_actors_queueEquipmentModelLoad(@ptrCast(actor), arg0, bladeWepId, 0, bladeMaterial);
        c.vs_actors_queueEquipmentModelLoad(@ptrCast(actor), arg0, shieldWepId, 1, shieldMaterial);
        c.vs_actors_queueActorWeaponSequenceLoad(@ptrCast(&temp.unk0), arg0, @as(c_int, actor.equippedWeaponCategory), arg7);
    }
    return temp;
}
