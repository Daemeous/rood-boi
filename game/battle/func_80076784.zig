//! An actor's stats from its init data (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// A stat from the init data: varied by -10..10 (vs_main_getRandSmoothed(21)) when asked, kept in 0..999.
fn vary(base: i16, varied: c_int) i16 {
    var value = base;
    if (varied != 0) {
        value = psx.conv(i16, @as(c_int, value) +% (-10 +% c.vs_main_getRandSmoothed(21)));
    }
    if (value < 0) {
        value = 0;
    } else if (value > 999) {
        value = 999;
    }
    return value;
}

/// orig: func_80076784 (src/BATTLE/BATTLE.PRG/146C.c)
/// Fills actor `slot`'s stats from its init data: name, HP, MP (unless 0), strength, intelligence and agility
/// (each varied by -10..10 when `varied`, kept in 0..999), speeds (nine tenths of the walk and run speeds), class,
/// reach, the weapon, shield and accessory (their ranges plus the reach), each limb's HP, defense, part and name
/// (func_800A152C/func_800A1648), types, affinities, armor, actions and DP splash, and its statuses (the top bit
/// when unk28 is negative).
pub fn func_80076784(slot: c_int, actor: ?*v.vs_battle_actor2, init: ?*v.vs_battle_charInitData, varied: c_int) callconv(.c) void {
    const a = actor.?;
    const d = init.?;
    a.unk957 = psx.conv(u8, slot);
    for (0..24) |k| {
        a.name[k] = d.name[k];
    }
    var value = vary(d.hp, varied);
    a.maxHP = value;
    a.currentHP = value;
    a.risk = 0;
    value = d.mp;
    if (value != 0) {
        value = vary(value, varied);
    }
    a.maxMP = value;
    a.currentMP = value;
    value = vary(d.strength, varied);
    a.totalStrength = psx.conv(u16, value);
    a.strength = psx.conv(u16, value);
    value = vary(d.intelligence, varied);
    a.totalIntelligence = psx.conv(u16, value);
    a.intelligence = psx.conv(u16, value);
    value = vary(d.agility, varied);
    a.totalAgility = psx.conv(u16, value);
    a.agility = psx.conv(u16, value);
    a.unk30 = d.unk24;
    a.unk32 = d.unk26;
    a.unk2E = psx.conv(u8, @divTrunc(@as(c_int, d.walkSpeed) * 9, 10)); // a byte times 9: no overflow
    a.unk31 = a.unk2E;
    a.unk2F = psx.conv(u8, @divTrunc(@as(c_int, d.runSpeed) * 9, 10)); // a byte times 9: no overflow
    a.unk33 = a.unk2F;
    psx.setBits(a, "enemyClass", psx.getBits(d, "unk3_5"));
    psx.setBits(a, "reach", psx.getBits(d, "reach"));
    psx.setBits(a, "unk956_2", psx.conv(u6, d.unk23));
    psx.setBits(a, "unk34", d.unk30);
    psx.setBits(a, "unk35", d.unk31);
    psx.setBits(a, "unk36", d.unk32);
    psx.setBits(a, "unk956_0", 0);
    psx.setBits(a, "isAnalyzed", 0);
    c.vs_battle_applyWeaponStats(@ptrCast(&a.weapon), @ptrCast(&d.weapon));
    const reach: c_int = psx.getBits(a, "reach");
    psx.setBits(a, "currentRange", psx.conv(u8, @as(c_int, a.weapon.range.range) + reach));
    psx.setBits(a, "unk39", psx.conv(u8, @as(c_int, a.weapon.range.unk1) + reach));
    psx.setBits(a, "unk3A", psx.conv(u8, @as(c_int, a.weapon.range.unk2) + reach));
    psx.setBits(a, "currentAttackShapeAngle", psx.getBits(&a.weapon.range, "attackShapeAngle"));
    psx.setBits(a, "unk3B_0", 1);
    c.vs_battle_applyShieldStats(@ptrCast(&a.shield), @ptrCast(&d.shield));
    c._applyAccessoryStats(@ptrCast(&a.accessory), @ptrCast(&d.accessory));
    var i: c_int = 0;
    while (i < 6) : (i += 1) { // i < 6
        const limb = &a.limbs[@intCast(i)];
        const source = &d.limbs[@intCast(i)];
        limb.maxHp = psx.conv(i16, source.hp);
        limb.hp = limb.maxHp;
        limb.agilityDefenseBonus = psx.conv(i8, source.agilityDefenseBonus);
        const part = c.func_800A152C(slot, i, 2);
        if (part >= 0) {
            limb.unk5 = psx.conv(u8, part);
            limb.nameIndex = psx.conv(u8, c.func_800A1648(slot, part, 0));
        } else {
            limb.nameIndex = 0;
            limb.unk5 = 0;
        }
        limb.chainEvasion = source.chainEvasion;
        for (0..4) |k| {
            limb.types[k] = source.types[k];
        }
        for (0..7) |k| {
            limb.affinities[k] = source.affinities[k];
        }
        c._applyArmorStats(@ptrCast(&limb.armor), @ptrCast(&source.unk20));
    }
    c.vs_battle_nop0(@ptrCast(a));
    for (0..6) |l| {
        for (0..4) |k| {
            const slot_ = &a.armor[l][k];
            const act = &d.limbs[l].actions[k];
            slot_.id = @bitCast(act.id);
            psx.setBits(slot_, "index", psx.conv(u4, act.index));
            psx.setBits(slot_, "unk2_4", psx.conv(u4, act.unk2));
            slot_.unk3 = 1;
        }
    }
    for (0..6) |l| {
        for (0..6) |k| {
            a.dpSplash[l][k] = d.limbs[l].dpSplash[k];
        }
    }
    a.unk944 = d.unk28;
    a.statuses = 0;
    if (a.unk944 < 0) {
        a.statuses = @bitCast(@as(c_uint, 0x80000000));
    }
    for (0..5) |k| {
        a.statusTimers[k] = 0;
    }
    a.unk954 = 0;
}
