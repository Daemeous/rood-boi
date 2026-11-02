//! The room's enemies and objects spawned from its MPD spawn list (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

fn flag(id: c_int) c_int {
    return c.vs_battle_getStateFlag(psx.conv(c_short, id));
}

/// The zone's ZUD buffer freed and forgotten.
fn dropZud() void {
    const zone = v.g._zoneContext();
    if (psx.get(zone.unk1C) != null) {
        c.vs_main_freeHeapR(psx.get(zone.unk1C));
    }
    psx.put(&zone.unk1C, null);
    zone.unk14 = 0;
}

/// orig: func_80089114 (src/BATTLE/BATTLE.PRG/146C.c)
/// With spawning on (D_800F18E0), reserves the slots the list names, then spawns each entry: an enemy (type 0)
/// that passes its scene, flag and stat tests, facing as listed (0/3 -> 6, 1 -> 0, 2 -> 1; 0x80 with unk24), into
/// its slot or the first free of 2..9 (a two-part enemy, D_8004FE88, also takes slot + 4), as a plain model for id
/// 0x7F (func_800774FC) or a full enemy (func_800775C0); an object whose flag is in range into its slot or the first
/// free of 2..15 (func_800765B0). No free slot: vs_main_nop10(0x86) and it stops. Then the saved states take the
/// hidden flags, and the ZUD buffer is freed.
pub fn func_80089114() callconv(.c) void {
    var slots: [16]c_int = undefined;
    // The C's data union and param keep their values from one entry to the next; param is uninitialised in the
    // original only for a first enemy whose facing is not 0..3 (0 here).
    var data: [4]u8 = .{ 0, 0, 0, 0 };
    var param: u16 = 0;
    const zone = v.g._zoneContext();
    const actors = v.g.vs_battle_actors();
    if (psx.global(c_int, "D_800F18E0").* == 0) {
        return;
    }
    var j: c_int = 15;
    while (j >= 0) : (j -= 1) { // j >= 0
        slots[@intCast(j)] = 0;
    }
    const spawns: [*]v._mpdEnemy = @ptrCast(psx.get(zone.mpdEnemies).?);
    var i: c_int = 0;
    while (psx.conv(c_uint, i) < zone.mpdEnemyCount) : (i += 1) { // i below a u32
        const enemy: *v.mpdEnemySpawn = @ptrCast(&spawns[@intCast(i)]);
        var slot: u16 = undefined;
        if (enemy.@"type" == 0) {
            slot = enemy.slot;
        } else {
            slot = @as(*v.mpdObjectSpawn, @ptrCast(enemy)).slot;
        }
        if (slot < 16) {
            slots[slot] = 1;
        }
    }
    i = 0;
    while (psx.conv(c_uint, i) < zone.mpdEnemyCount) : (i += 1) { // i below a u32
        const enemy: *v.mpdEnemySpawn = @ptrCast(&spawns[@intCast(i)]);
        if (enemy.@"type" == 0) {
            var visible = enemy.hidden == 0;
            switch (enemy.sceneTest) {
                1 => if (flag(1) != 0) {
                    visible = false;
                },
                2 => if (flag(1) != 1) {
                    visible = false;
                },
                3 => if (flag(2) != 1) {
                    visible = false;
                },
                else => {},
            }
            switch (enemy.flagTest) {
                1 => if (flag(0x82) != enemy.flagValue) {
                    visible = false;
                },
                2 => if (flag(0x82) > enemy.flagValue) {
                    visible = false;
                },
                3 => if (flag(0x82) < enemy.flagValue) {
                    visible = false;
                },
                else => {},
            }
            const hp: c_int = psx.get(v.g.vs_battle_characterState().*.?.stats).?.currentHP;
            switch (enemy.statTest) {
                1 => if ((enemy.statValue & 0x3FF) < hp) {
                    visible = false;
                },
                2 => if (hp < (enemy.statValue & 0x3FF)) {
                    visible = false;
                },
                3 => if (flag(enemy.statValue & 0x3FF) != (enemy.statValue >> 10)) {
                    visible = false;
                },
                4 => if (flag(enemy.statValue & 0x3FF) > (enemy.statValue >> 10)) {
                    visible = false;
                },
                5 => if (flag(enemy.statValue & 0x3FF) < (enemy.statValue >> 10)) {
                    visible = false;
                },
                else => {},
            }
            const id: u16 = enemy.id;
            switch (enemy.facing) {
                0, 3 => param = 6,
                1 => param = 0,
                2 => param = 1,
                else => {},
            }
            if (enemy.unk24 != 0) {
                param |= 0x80;
            }
            const template = &@as([*]v._zndEnemy, @ptrCast(psx.get(zone.zndEnemies).?))[id];
            const two_part = psx.globalMany(u8, "D_8004FE88")[template.unk0[2]] != 0;
            var slot: u16 = enemy.slot;
            // slots has 16 entries: the list's slots stay below 12 (slot + 4 is not bounded by the C).
            if (slot == 0) {
                j = 2;
                while (j < 10) : (j += 1) { // j < 10
                    if (actors[@intCast(j)] == null and slots[@intCast(j)] == 0) {
                        if (two_part) {
                            if (actors[@intCast(j + 4)] != null or slots[@intCast(j + 4)] != 0) {
                                continue;
                            }
                            slots[@intCast(j + 4)] = 1;
                        }
                        slot = @intCast(j); // 2..9
                        break;
                    }
                }
                if (j >= 10) {
                    c.vs_main_nop10(0x86, 0);
                    dropZud();
                    return;
                }
            } else if (two_part) {
                if (actors[slot + 4] == null and slots[slot + 4] == 0) {
                    slots[slot + 4] = 1;
                } else {
                    c.vs_main_nop10(0x86, 0);
                    dropZud();
                    return;
                }
            }
            if (visible) {
                if (id == 0x7F) {
                    data[0] = enemy.x;
                    data[2] = enemy.z;
                    data[1] = enemy.direction;
                    data[3] = enemy.height << 6;
                    _ = c.func_800774FC(slot, 0x7F, 0, 0, 0, 0, @ptrCast(&data), param);
                } else {
                    _ = c.func_800775C0(slot, @ptrCast(enemy), @ptrCast(template), param);
                }
            }
        } else {
            const object: *v.mpdObjectSpawn = @ptrCast(enemy);
            const value: c_uint = @intCast(flag(object.flag & 0x3FF)); // a byte
            const min: c_uint = object.flagMin;
            const max: c_uint = object.flagMax;
            if (value >= min and value <= max) {
                data[0] = object.x;
                data[2] = object.z;
                data[1] = object.y;
                data[3] = object.direction *% 64;
                var slot: u16 = object.slot;
                if (slot == 0xFF) {
                    j = 2;
                    while (j < 16) : (j += 1) { // j < 16
                        if (actors[@intCast(j)] == null and slots[@intCast(j)] == 0) {
                            slot = @intCast(j); // 2..15
                            break;
                        }
                    }
                    if (j >= 16) {
                        c.vs_main_nop10(0x86, 0);
                        return;
                    }
                }
                param = object.model;
                const model: c_uint = param;
                _ = c.func_800765B0(slot, @bitCast((model >> 12) | (model & 0xF00)), @ptrCast(&data),
                    @bitCast(((model >> 3) & 0x1C) | (data[3] / 64)), 0);
            }
        }
    }
    i = 0;
    while (i < 32) : (i += 1) { // i < 32
        const enemy: *v.mpdEnemySpawn = @ptrCast(&spawns[@intCast(i)]);
        const saved: ?*v.savedEnemyState = @ptrCast(@alignCast(c.func_80069E80(i)));
        if (saved) |state| {
            state.hidden = enemy.hidden;
        }
    }
    dropZud();
}
