//! Spawns an enemy: its actor block, its ZUD models loaded and requested, its setup (src/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

fn at(comptime T: type, base: *anyopaque, offset: usize) *T {
    return @ptrCast(@alignCast(@as([*]u8, @ptrCast(base)) + offset));
}

/// The saved state of this spawn when the room is the one the save names (D_80061078[D_80060064]).
fn savedState(index: u8) ?*v.savedEnemyState {
    const zone = v.g._zoneContext();
    const place = psx.at(v.g.D_80061078(), v.g.D_80060064().*);
    if (@as(c_int, place.zndId) == zone.zndId and @as(c_int, place.mapId) == zone.mapId) {
        return @ptrCast(@alignCast(c.func_80069E80(index)));
    }
    return null;
}

/// orig: func_800775C0 (src/BATTLE/BATTLE.PRG/146C.c)
/// For an empty slot `id`: allocates the actor block (a two-part enemy, D_8004FE88, gets a second model block
/// instead of a shield's), loads its ZUD file (kept when it is the one already loaded), requests its model (at
/// its saved place, else the spawn's), its second part or weapon and shield, and its weapon and own effects, waits
/// for the queue, then sets it up (func_80076F24 and the animation, speed, list and spawn fields, its saved stats)
/// and returns it; null for a slot in use.
pub fn func_800775C0(id: c_int, spawn: ?*v.mpdEnemySpawn, init: ?*v.vs_battle_charInitData, flags: c_int) callconv(.c) ?*v.vs_battle_actor {
    // The C's request local starts uninitialised; vs_battle_populateDataSlot copies all of it into a slot (unk11
    // before its first store): those bytes are 0 here (PORTING.md §11.4).
    var object = std.mem.zeroes(v.vs_battle_objectData);
    var file: v.vs_main_CdFile = undefined;
    const zone = v.g._zoneContext();
    const actors = v.g.vs_battle_actors();
    const enemy = spawn.?;
    const data = init.?;
    // the slot is not bounded by the C
    if (psx.at(actors, id).* != null) {
        return null;
    }
    const zudIndex = data.mpdIdentifer;
    const bladeId: c_int = @as(u8, @bitCast(data.weapon.blade.wepId));
    const shieldId: c_int = @as(u8, @bitCast(data.shield.base.wepId));
    const two_part = psx.globalMany(u8, "D_8004FE88")[data.unk2] != 0;
    var actor: *v.vs_battle_actor = undefined;
    if (two_part) {
        const block = c.vs_main_allocHeap(0x3BB4).?;
        actor = @ptrCast(@alignCast(block));
        psx.put(&actor.stats, at(v.vs_battle_actor2, block, 0x50));
        actor.unk40 = 2;
        psx.put(&actor.unk44, at(v.D_800F4538_unk0, block, 0x9B4));
        psx.put(&actor.unk48[0], at(v.vs_battle_actor4, block, 0x22B4));
    } else {
        const block = c.vs_main_allocHeap(0x2E84).?;
        actor = @ptrCast(@alignCast(block));
        psx.put(&actor.stats, at(v.vs_battle_actor2, block, 0x50));
        actor.unk40 = 1;
        psx.put(&actor.unk44, at(v.D_800F4538_unk0, block, 0x9B4));
        psx.put(&actor.unk48[0], at(v.vs_battle_actor4, block, 0x22B4));
        psx.put(&actor.unk48[1], at(v.vs_battle_actor4, block, 0x289C));
    }
    var category: c_int = 10;
    if (bladeId != 0) {
        category = data.weapon.blade.category;
    }
    actor.equippedWeaponCategory = psx.conv(u8, category);
    var loaded = false;
    if (psx.get(zone.unk1C) != null) {
        if (zudIndex +% 1 == zone.unk14) {
            loaded = true;
        } else {
            c.vs_main_freeHeapR(psx.get(zone.unk1C));
            psx.put(&zone.unk1C, null);
            zone.unk14 = 0;
        }
    }
    if (!loaded) {
        file.lba = psx.at(psx.get(zone.zudFiles).?, zudIndex).lba;
        file.size = psx.at(psx.get(zone.zudFiles).?, zudIndex).size;
        if (psx.get(zone.unk18) != null) {
            c.vs_main_nop9(0xA7, 0);
        }
        psx.put(&zone.unk18, @as(?*v.vs_main_CdQueueSlot, @ptrCast(@alignCast(c.vs_main_allocateCdQueueSlot(@ptrCast(&file))))));
        psx.put(&zone.unk1C, c.vs_main_allocHeapR(psx.conv(c_uint, file.size)));
        c.vs_main_cdEnqueuePriority(@ptrCast(psx.get(zone.unk18)), psx.get(zone.unk1C));
        while (psx.get(zone.unk18).?.state != 4) {
            _ = c.vs_main_gametimeUpdate(0);
        }
        c.vs_main_freeCdQueueSlot(@ptrCast(psx.get(zone.unk18)));
        psx.put(&zone.unk18, null);
        zone.unk14 = zudIndex +% 1;
    }
    const zud: *v.zudHeader = @ptrCast(@alignCast(psx.get(zone.unk1C).?));
    const zud_addr = psx.addr(zud);
    object.dataType = 2;
    object.index = psx.conv(i8, id);
    object.dataAddr = @as(u32, @bitCast(zud.model)) +% zud_addr;
    object.modelId = psx.at(@as([*]v._zndEnemy, @ptrCast(psx.get(zone.zndEnemies).?)), zudIndex).unk0[2];
    var height: c_int = undefined;
    if (savedState(enemy.index)) |saved| {
        psx.setBits(&object.unkC, "unk0_0", saved.x);
        psx.setBits(&object.unkC, "unk0_16", saved.z);
        psx.setBits(&object.unkC, "unk0_8", saved.direction);
        height = saved.height;
        psx.setBits(&object.unkC, "unk0_8", psx.getBits(&object.unkC, "unk0_8") & 1);
    } else {
        psx.setBits(&object.unkC, "unk0_0", enemy.x);
        psx.setBits(&object.unkC, "unk0_16", enemy.z);
        psx.setBits(&object.unkC, "unk0_8", enemy.direction);
        height = @as(c_int, enemy.height) << 6;
        psx.setBits(&object.unkC, "unk0_8", psx.getBits(&object.unkC, "unk0_8") & 1);
    }
    psx.setBits(&object.unkC, "unk0_24", psx.conv(u8, height));
    psx.put(&object.unk4, @as(?*anyopaque, psx.get(actor.unk44)));
    object.actorId = 0xFF;
    object.variant = enemy.variant & 1;
    object.material = data.unk24;
    _ = c.vs_battle_populateDataSlot(@ptrCast(&object));
    if (data.unk2 != 0x7F) {
        if (psx.globalMany(u8, "D_8004FE88")[data.unk2] != 0) {
            object.dataType = 1;
            object.index = psx.conv(i8, id +% 4);
            const templates: [*]v.vs_battle_charInitData = @ptrCast(@alignCast(psx.get(zone.zndEnemies).?));
            object.modelId = @as(i16, psx.at(templates, zudIndex).unk2) + 1;
            psx.put(&object.unk4, @as(?*anyopaque, psx.get(actor.unk48[0])));
            object.actorId = psx.conv(u8, id);
            object.unk11 = 0xFC;
            object.variant = 0;
            _ = c.vs_battle_populateDataSlot(@ptrCast(&object));
        } else {
            if (bladeId != 0) {
                object.dataType = 4;
                object.index = psx.conv(i8, id *% 2);
                object.dataAddr = @as(u32, @bitCast(zud.blade)) +% zud_addr;
                object.modelId = @intCast(bladeId); // a byte
                psx.put(&object.unk4, @as(?*anyopaque, psx.get(actor.unk48[0])));
                object.actorId = psx.conv(u8, id);
                object.unk11 = 0xF0;
                object.material = psx.conv(u8, data.weapon.material);
                _ = c.vs_battle_populateDataSlot(@ptrCast(&object));
            }
            if (shieldId != 0) {
                object.dataType = 4;
                object.index = psx.conv(i8, id *% 2 +% 1);
                object.dataAddr = @as(u32, @bitCast(zud.shield)) +% zud_addr;
                object.modelId = @intCast(shieldId); // a byte
                psx.put(&object.unk4, @as(?*anyopaque, psx.get(actor.unk48[1])));
                object.actorId = psx.conv(u8, id);
                object.unk11 = 0xF1;
                object.material = psx.conv(u8, data.shield.material);
                _ = c.vs_battle_populateDataSlot(@ptrCast(&object));
            }
        }
        if ((id < 2 or (flags & 3) == 1) and zud.weaponEffectSize != 0) {
            object.dataType = 8;
            object.index = psx.conv(i8, id);
            object.dataAddr = @as(u32, @bitCast(zud.weaponEffect)) +% zud_addr;
            object.actorId = 0;
            object.modelId = actor.equippedWeaponCategory;
            _ = c.vs_battle_populateDataSlot(@ptrCast(&object));
        }
        if (zud.effectSize != 0) {
            object.dataType = 8;
            object.index = psx.conv(i8, id);
            object.dataAddr = @as(u32, @bitCast(zud.effect)) +% zud_addr;
            object.actorId = 0;
            object.modelId = 0;
            _ = c.vs_battle_populateDataSlot(@ptrCast(&object));
        }
    }
    while (c.vs_battle_getEmptyObjectDataSlot() != 0) {
        _ = c.vs_battle_processObjectDataQueue();
        _ = c.vs_main_gametimeUpdate(0);
    }
    psx.at(actors, id).* = actor;
    c.func_80076F24(id, @ptrCast(data), bladeId, shieldId, flags, 1);
    if (actor.weaponDrawn & 1 != 0) {
        c.func_800A087C(id, 0x1846);
    } else {
        c.func_800A087C(id, 0x46);
    }
    _ = c.func_800A0204(id, 0, 0, 0);
    const stats = psx.get(actor.stats).?;
    c.func_800A0AFC(id, @as(c_int, stats.unk31) << 12, @as(c_int, psx.get(actor.stats).?.unk33) << 12);
    if (actor.allegiance & 7 != 0) {
        psx.put(&actor.next, psx.get(actors[0].?.next)); // the C copies the raw word
        psx.put(&actors[0].?.next, actor);
    }
    actor.unk1E = enemy.facing;
    actor.unk1F = enemy.unk26;
    actor.unk27 = enemy.index;
    actor.unk29 = 0;
    actor.defeated = 0;
    stats.miscItem.id = enemy.itemId;
    stats.miscItem.count = enemy.itemCount;
    psx.byteAt(u16, stats, 0x95C).* = enemy.item2Id;
    psx.byteAt(u8, stats, 0x95E).* = enemy.item2Count;
    psx.byteAt(u16, stats, 0x960).* = enemy.item3Id;
    psx.byteAt(u8, stats, 0x962).* = enemy.item3Count;
    psx.byteAt(v.actorSpawnSettings, actor, 0x30).* = enemy.settings;
    actor.unk38 = data.unk2C;
    var result: c_int = undefined;
    if (savedState(enemy.index)) |saved| {
        stats.currentHP = saved.currentHP;
        stats.maxHP = saved.maxHP;
        stats.currentMP = saved.currentMP;
        stats.maxMP = saved.maxMP;
        stats.totalStrength = @bitCast(saved.strength);
        stats.strength = stats.totalStrength;
        stats.totalIntelligence = @bitCast(saved.intelligence);
        stats.intelligence = stats.totalIntelligence;
        stats.totalAgility = @bitCast(saved.agility);
        stats.agility = stats.totalAgility;
        for (0..6) |k| {
            stats.limbs[k].hp = saved.limbHP[k];
        }
        c.func_80086FA8(saved.statuses, @ptrCast(stats));
        result = c.func_800E6178(@ptrCast(actor), saved.unk2);
    } else {
        result = c.func_800E6178(@ptrCast(actor), -1);
    }
    if (result == 0) {
        c.func_8009DF3C(id, enemy.unk26);
    } else {
        c.func_8009DF3C(id, 0);
    }
    return actor;
}
