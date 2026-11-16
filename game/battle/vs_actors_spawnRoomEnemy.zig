//! Spawns an enemy: its actor block, its ZUD models loaded and requested, its setup (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_146C");
const c = @import("c_146C");

fn at(comptime T: type, base: *anyopaque, offset: usize) *T {
    return @ptrCast(@alignCast(@as([*]u8, @ptrCast(base)) + offset));
}

/// The saved state of this spawn when the room is the one the save names (D_80061078[vs_state_currentRoomStateSlot]).
fn savedState(index: u8) ?*v.savedEnemyState {
    const zone = v.g._zoneContext();
    const place = psx.at(v.g.D_80061078(), v.g.vs_state_currentRoomStateSlot().*);
    if (@as(c_int, place.zndId) == zone.zndId and @as(c_int, place.mapId) == zone.mapId) {
        return @ptrCast(@alignCast(c.vs_actors_findRememberedEnemy(index)));
    }
    return null;
}

/// orig: vs_actors_spawnRoomEnemy (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// For an empty slot `id`: allocates the actor block (a two-part enemy, vs_actors_usesSecondaryBodyModel, gets a second model block
/// instead of a shield's), loads its ZUD file (kept when it is the one already loaded), requests its model (at
/// its saved place, else the spawn's), its second part or weapon and shield, and its weapon and own effects, waits
/// for the queue, then sets it up (vs_actors_initializeActorFromTemplate and the animation, speed, list and spawn fields, its saved stats)
/// and returns it; null for a slot in use.
pub fn vs_actors_spawnRoomEnemy(id: c_int, spawn: ?*v.mpdEnemySpawn, init: ?*v.vs_battle_charInitData, flags: c_int) callconv(.c) ?*v.vs_battle_actor {
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
    const two_part = psx.GlobalMany(u8, "vs_actors_usesSecondaryBodyModel").ptr()[data.unk2] != 0;
    var actor: *v.vs_battle_actor = undefined;
    // The block: the actor, its stats, its object (the model), then the unk48 records: two weapon records,
    // or for a two-part enemy the second part's object.
    const stats_at = @offsetOf(v.vs_battle_actor_dat2, "unk0") + @offsetOf(@FieldType(v.vs_battle_actor_dat2, "unk0"), "unk50");
    const object_at = @offsetOf(v.vs_battle_actor_dat2, "unk9B4");
    const records_at = @offsetOf(v.vs_battle_actor_dat, "unk22B4");
    const object_size = common.object.region(v.D_800F4538_t);
    if (two_part) {
        const block = c.vs_main_allocHeap(@intCast(records_at + object_size)).?;
        actor = @ptrCast(@alignCast(block));
        psx.put(&actor.stats, at(v.vs_battle_actor2, block, stats_at));
        actor.unk40 = 2;
        psx.put(&actor.unk44, at(v.D_800F4538_unk0, block, object_at));
        psx.put(&actor.unk48[0], at(v.vs_battle_actor4, block, records_at));
    } else {
        const block = c.vs_main_allocHeap(@intCast(@sizeOf(v.vs_battle_actor_dat))).?;
        actor = @ptrCast(@alignCast(block));
        psx.put(&actor.stats, at(v.vs_battle_actor2, block, stats_at));
        actor.unk40 = 1;
        psx.put(&actor.unk44, at(v.D_800F4538_unk0, block, object_at));
        psx.put(&actor.unk48[0], at(v.vs_battle_actor4, block, records_at));
        psx.put(&actor.unk48[1], at(v.vs_battle_actor4, block, records_at + @sizeOf(v.vs_battle_actor4)));
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
        c.vs_main_cdEnqueuePriority(@ptrCast(@alignCast(psx.get(zone.unk18))), psx.get(zone.unk1C));
        while (psx.get(zone.unk18).?.state != 4) {
            _ = c.vs_main_gametimeUpdate(0);
        }
        c.vs_main_freeCdQueueSlot(@ptrCast(@alignCast(psx.get(zone.unk18))));
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
        if (psx.GlobalMany(u8, "vs_actors_usesSecondaryBodyModel").ptr()[data.unk2] != 0) {
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
    c.vs_actors_initializeActorFromTemplate(id, @ptrCast(data), bladeId, shieldId, flags, 1);
    if (actor.weaponDrawn & 1 != 0) {
        c.vs_actors_setActorDisplayFlags(id, 0x1846);
    } else {
        c.vs_actors_setActorDisplayFlags(id, 0x46);
    }
    _ = c.vs_actors_playActorAnimation(id, 0, 0, 0);
    const stats = psx.get(actor.stats).?;
    c.vs_actors_setActorWalkRunSpeeds(id, @as(c_int, stats.unk31) << 12, @as(c_int, psx.get(actor.stats).?.unk33) << 12);
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
    // the spawn's 8 setting bytes fill flags30 and unk34
    actor.flags30.raw = std.mem.readInt(u32, enemy.settings.bytes[0..4], .little);
    actor.unk34 = std.mem.readInt(i32, enemy.settings.bytes[4..8], .little);
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
        c.vs_battle_inflictActorStatuses(saved.statuses, @ptrCast(stats));
        result = c.vs_battle_aiCreateActorAiState(@ptrCast(actor), saved.unk2);
    } else {
        result = c.vs_battle_aiCreateActorAiState(@ptrCast(actor), -1);
    }
    if (result == 0) {
        c.func_8009DF3C(id, enemy.unk26);
    } else {
        c.func_8009DF3C(id, 0);
    }
    return actor;
}
