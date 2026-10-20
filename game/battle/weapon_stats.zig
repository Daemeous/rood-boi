//! A weapon's stats from its parts (src/BATTLE/BATTLE.PRG/146C.c).
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// orig: vs_battle_applyWeaponStats (src/BATTLE/BATTLE.PRG/146C.c)
/// Fills a UI weapon from its blade, grip and gems: the parts, its identity, and the summed stats, classes and affinities.
pub fn applyWeaponStats(target: ?*v.vs_battle_uiWeapon, source: ?*v.vs_battle_weaponIntermediate) callconv(.c) void {
    const t = target.?;
    const s = source.?;
    c.vs_main_memcpy(&t.blade, &s.blade, @sizeOf(v.vs_battle_uiEquipment));
    c.vs_main_memcpy(&t.grip, &s.grip, @sizeOf(v.vs_battle_uiEquipment));

    var i: c_int = 0;
    while (i < 3) : (i += 1) {
        // i < 3: the gem count.
        c.vs_main_memcpy(&t.gems[@intCast(i)], &s.gems[@intCast(i)], @sizeOf(v.vs_battle_uiEquipment));
    }

    t.wepId = psx.conv(u8, s.blade.wepId);
    t.dropRate = s.dropRate;
    t.index = psx.conv(u8, s.index);
    t.blade.material = s.material;
    t.damageType = s.blade.damageType;
    t.actionType = s.blade.costType;
    // grip.types[damageType]: the C does not check the damage type.
    t.damageTypeValue = psx.conv(u8, psx.at(&s.grip.types, t.damageType).*);
    t.risk = s.blade.cost;
    t.unk10B = s.blade.unk14;
    t.range = s.blade.range;
    t.currentDp = s.blade.currentDp;
    t.maxDp = s.blade.maxDp;

    t.currentPp = s.blade.currentPp;
    t.maxPp = s.blade.maxPp;
    t.baseStr = psx.conv(u16, @as(c_int, s.blade.strength) +% s.grip.strength +% s.gems[0].strength +%
        s.gems[1].strength +% s.gems[2].strength);
    t.currentStr = psx.conv(i16, t.baseStr);
    t.baseInt = psx.conv(u16, @as(c_int, s.blade.intelligence) +% s.grip.intelligence +% s.gems[0].intelligence +%
        s.gems[1].intelligence +% s.gems[2].intelligence);
    t.currentInt = psx.conv(i16, t.baseInt);
    t.baseAgility = psx.conv(u16, @as(c_int, s.blade.agility) +% s.grip.agility +% s.gems[0].agility +%
        s.gems[1].agility +% s.gems[2].agility);
    t.currentAgility = psx.conv(i16, t.baseAgility);

    i = 0;
    while (i < 6) : (i += 1) {
        // i < 6: inside the 8 classes.
        const k: usize = @intCast(i);
        t.classAffinityCurrent.class[1][k] = psx.conv(i16, @as(c_int, s.blade.classes[k]) +% s.gems[0].classes[k] +%
            s.gems[1].classes[k] +% s.gems[2].classes[k]);
        t.classAffinityCurrent.class[0][k] = t.classAffinityCurrent.class[1][k];
        t.classAffinityBaseline.class[k] = t.classAffinityCurrent.class[0][k];
    }

    i = 0;
    while (i < 7) : (i += 1) {
        // i < 7: inside the 8 affinities.
        const k: usize = @intCast(i);
        t.classAffinityCurrent.affinity[1][k] = psx.conv(i16, @as(c_int, s.blade.affinities[k]) +% s.gems[0].affinities[k] +%
            s.gems[1].affinities[k] +% s.gems[2].affinities[k]);
        t.classAffinityCurrent.affinity[0][k] = t.classAffinityCurrent.affinity[1][k];
        t.classAffinityBaseline.affinity[k] = psx.conv(u16, t.classAffinityCurrent.affinity[0][k]);
        t.unk168[k] = 0;
    }

    i = 0;
    while (i < 24) : (i += 1) {
        // i < 24: the name's length.
        t.name[@intCast(i)] = s.name[@intCast(i)];
    }
}
