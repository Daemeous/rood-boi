//! Picks one equipped shield or limb of a battle actor at random and applies the class and weapon affinity changes to it (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_items_growDefenderEquipmentRatings (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Chooses a random equipped shield or limb of `arg1` for the action `arg0` and triggers its class and weapon affinity changes.
pub fn vs_items_growDefenderEquipmentRatings(arg0: ?*v.vs_action_t, arg1: ?*v.vs_battle_actor2, arg2: c_int, arg3: c_int, arg4: c_int) callconv(.c) void {
    const action = arg0.?;
    var sp10: [8]c_int = undefined;
    var count: c_int = 0;

    // The switch on unk2_4, with the C's fall-through from cases 5/7/14 into 6/8/11/12 as two tests.
    const kind: u8 = switch (@as(c_int, psx.getBits(action, "unk2_4"))) {
        1, 2, 3, 4 => 1,
        5, 7, 14 => 2,
        6, 8, 11, 12 => 3,
        else => 0,
    };

    if (kind == 1) {
        const actor = arg1.?;
        if ((psx.at(v.g.vs_battle_actors(), actor.actorId).*.?.weaponDrawn & 1) != 0 and actor.shield.base.id != 0) {
            sp10[@intCast(count)] = 0xFF;
            count +%= 1;
        }
        var i: usize = 0;
        while (i < 6) : (i += 1) {
            if (actor.limbs[i].maxHp != 0 and actor.limbs[i].armor.armor.id != 0) {
                sp10[@intCast(count)] = psx.conv(c_int, i);
                count +%= 1;
            }
        }
    }

    if (kind == 2) {
        const actor = arg1.?;
        if ((psx.at(v.g.vs_battle_actors(), actor.actorId).*.?.weaponDrawn & 1) != 0) {
            if (actor.shield.base.id != 0) {
                sp10[@intCast(count)] = 0xFF;
                count +%= 1;
            }
        }
    }

    if (kind == 2 or kind == 3) {
        const actor = arg1.?;
        const limb = psx.at(&actor.limbs, arg4);
        if (limb.maxHp != 0 and limb.armor.armor.id != 0) {
            sp10[@intCast(count)] = arg4;
            count +%= 1;
        }
    }

    if (count == 0) return;

    const pick: c_int = sp10[@intCast(c.vs_main_getRand(count))];

    if (pick == 0xFF) {
        const subject = arg1.?;
        const equipment = &subject.shield.base;
        if (subject.shield.base.id != 0) {
            if (arg2 != 0) {
                c._triggerClassChange(@ptrCast(arg0), @ptrCast(equipment), arg2 -% 1, 0xF1, @as(c_int, subject.actorId));
            }
            if (arg3 != 0) {
                c._triggerWeaponAffinityChange(@ptrCast(arg0), @ptrCast(equipment), arg3 -% 1, 0xF1, @as(c_int, subject.actorId));
            }
        }
        c._calculateShieldClassAffinity(@ptrCast(subject));
    } else {
        const subject = arg1.?;
        const limb = psx.at(&subject.limbs, pick);
        const equipment = &limb.armor.armor;
        if (equipment.id != 0) {
            if (arg2 != 0) {
                c._triggerClassChange(@ptrCast(arg0), @ptrCast(equipment), arg2 -% 1, @as(c_int, limb.nameIndex), @as(c_int, subject.actorId));
            }
            if (arg3 != 0) {
                c._triggerWeaponAffinityChange(@ptrCast(arg0), @ptrCast(equipment), arg3 -% 1, @as(c_int, limb.nameIndex), @as(c_int, subject.actorId));
            }
        }
        c._calculateArmorClassAffinity(@ptrCast(subject), pick);
    }
}
