//! The action's cast started: its animation, sound and effect request (src/BATTLE/BATTLE.PRG/146C.c).
const std = @import("std");
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// D_800F19CC, read at each use as the C does.
fn state() *v.D_800F19CC_t {
    return v.g.D_800F19CC().*.?;
}

// The C's request is a local `actionEffectRequest` (146C.c), which it hands to func_800CF0E8 as the views'
// func_800CF0E8_t (both 0xE8 bytes): effect 0, count 2, otherCount 3, source 4, targets[16] 0x10 (12 bytes each:
// type 0, value 4: actor 4, reserved 5, part 6, or a SVECTOR position), others[16] 0xD0, position 0xE0.
const source_offset = 4;
const targets_offset = 0x10;
const target_size = 12;

/// The byte offset of targets[k] (k < 0: the source); not bounded, as in the C.
fn target(k: c_int) isize {
    return if (k < 0) source_offset else targets_offset + @as(isize, k) * target_size;
}

/// orig: func_8007138C (src/BATTLE/BATTLE.PRG/146C.c)
/// Starts the current link's cast: the ability input reset (attempted when the player is under status 0x8000); a
/// plain action is released at once (func_80070278). Otherwise the caster turns and sounds by the action's kind,
/// and the effect request is built (the caster's part, each target's part or position, every other enemy as a
/// bystander) and sent with its mode; a Break Art or battle ability also loads its caster's armour or weapon model;
/// the action's name is shown when the information setting is on, and the camera mode (phase) becomes 5.
pub fn startCast() callconv(.c) void {
    var object = std.mem.zeroes(v.vs_battle_objectData);
    var request: v.func_800CF0E8_t = undefined;
    var actor_position: v.func_8006EBF8_t = undefined;
    var sound_position: v.SVECTOR = undefined;

    _ = c.memset(&request, @as(c_int, 0), @as(usize, @sizeOf(v.func_800CF0E8_t)));
    state().battleAbilityInputSuccessful = 0;
    if (psx.get(v.g.vs_battle_characterState().*.?.stats).?.statuses & 0x8000 != 0) {
        state().battleAbilityInputAttempted = 1;
    } else {
        state().battleAbilityInputAttempted = 0;
    }

    const action = &state().unk854[state().unk0 & 3];
    // the action index is not bounded by the C
    if (psx.getBits(psx.at(v.g.actions(), action.actionIndex), "unk2_0") == 0) {
        c.func_80070278();
        return;
    }

    _ = c.func_800A1108(@as(c_int, action.unk4.unk0.targetActor), &actor_position);
    sound_position.vx = actor_position.unk0.unk4.vx;
    sound_position.vz = actor_position.unk0.unk4.vz;
    sound_position.vy = actor_position.unk0.unk4.vy;

    const caster: c_int = action.unk4.unk0.targetActor;
    var mode: c_int = undefined;
    if (action.actionIndex < 0x8D) {
        c.func_8009E2E0(caster, psx.cptr(c.SVECTOR, &state().unk8.unk844), 0);
        c.vs_main_panSfx(0x7E, 0x34, psx.cptr(c.SVECTOR, &sound_position));
        if (action.unk4.unk0.targetActor == 0) {
            c.vs_battle_setStateFlag(0xB7, 1);
        }
        mode = 2;
    } else if (action.actionIndex < 0xB8) {
        c.func_8009F298(caster, psx.cptr(c.SVECTOR, &state().unk8.unk844), 0);
        c.vs_main_panSfx(0x7E, 0x36, psx.cptr(c.SVECTOR, &sound_position));
        mode = 3;
    } else if (action.actionIndex < 0xE0) {
        c.func_8009EFEC(caster, psx.cptr(c.SVECTOR, &state().unk8.unk844), 0);
        c.vs_main_panSfx(0x7E, 0x34, psx.cptr(c.SVECTOR, &sound_position));
        if (action.unk4.unk0.targetActor == 0) {
            c.vs_battle_setStateFlag(0xB9, 1);
        }
        mode = 3;
    } else {
        c.func_8009E2E0(caster, psx.cptr(c.SVECTOR, &state().unk8.unk844), 0);
        c.vs_main_panSfx(0x7E, 0x35, psx.cptr(c.SVECTOR, &sound_position));
        mode = 1;
    }

    request.effectId = psx.at(v.g.actions(), action.actionIndex).unk1;
    request.unk3 = 0; // otherCount
    psx.byteAt(u16, &request, target(-1)).* = 4;
    psx.byteAt(u8, &request, target(-1) + 4).* = action.unk4.unk0.targetActor;
    psx.byteAt(u8, &request, target(-1) + 5).* = 0;
    psx.byteAt(i8, &request, target(-1) + 6).* = psx.conv(i8, c.func_800A152C(@as(c_int, action.unk4.unk0.targetActor), 255, 2));

    var count: c_int = 0;
    var i: c_int = 0;
    // the hit count is not bounded by the C: unk4C[i] and targets[count] through byte offsets
    while (i < action.unk4A) : (i += 1) {
        const hit = psx.at(&action.unk4C, i);
        if (hit.unk40 == 0) {
            var duplicate = false;
            var j: c_int = 0;
            while (j < count) : (j += 1) {
                if (psx.at(&action.unk4C, j).unk40 == 0 and
                    psx.byteAt(u8, &request, target(j) + 4).* == hit.unk0.targetActor)
                {
                    psx.byteAt(i8, &request, target(j) + 6).* = 0;
                    duplicate = true;
                }
            }
            if (duplicate) continue;
            psx.byteAt(u16, &request, target(count)).* = 4;
            psx.byteAt(u8, &request, target(count) + 4).* = hit.unk0.targetActor;
            psx.byteAt(u8, &request, target(count) + 5).* = 0;
            psx.byteAt(i8, &request, target(count) + 6).* = psx.conv(i8, c.func_800A152C(@as(c_int, hit.unk0.targetActor), @as(c_int, hit.unk0.targetLimb), 2));
            // Original bug: the limb tested is hit `count`'s, not hit `i`'s (the part is then set again either way).
            if (psx.at(&action.unk4C, count).unk0.targetLimb == -1) {
                const limb = psx.getBits(psx.get(psx.at(v.g.vs_battle_actors(), hit.unk0.targetActor).*.?.stats).?, "unk36");
                psx.byteAt(i8, &request, target(count) + 6).* = psx.conv(i8, c.func_800A152C(@as(c_int, hit.unk0.targetActor), @as(c_int, limb), 2));
            } else {
                psx.byteAt(i8, &request, target(count) + 6).* = psx.conv(i8, c.func_800A152C(@as(c_int, hit.unk0.targetActor), @as(c_int, hit.unk0.targetLimb), 2));
            }
            count += 1; // at most unk4A (u16)
        } else {
            psx.byteAt(u16, &request, target(count)).* = 5;
            psx.byteAt(v.SVECTOR, &request, target(count) + 4).* = @as(*align(1) const v.SVECTOR, @ptrCast(&hit.unk0)).*;
            count += 1; // at most unk4A (u16)
        }
    }
    request.unk2 = @bitCast(psx.conv(i8, count)); // count (signed char)

    var other: c_int = 0;
    while (other < 16) : (other += 1) {
        const slot = v.g.vs_battle_actors()[@intCast(other)]; // other < 16
        if (slot != null and (slot.?.unk40 == 1 or slot.?.unk40 == 2)) {
            var duplicate = false;
            var j: c_int = 0;
            while (j < action.unk4A) : (j += 1) {
                // Original bug: the C tests hit j's unk40 but hit `other`'s target (unk4C[i], not unk4C[j]).
                if (psx.at(&action.unk4C, j).unk40 == 0 and other == psx.at(&action.unk4C, other).unk0.targetActor) {
                    duplicate = true;
                    break;
                }
            }
            if (other == action.unk4.unk0.targetActor) {
                duplicate = true;
            }
            if (!duplicate) {
                request.unkD0[request.unk3] = psx.conv(u8, other); // others[otherCount]: at most 16 bystanders
                request.unk3 += 1;
            }
        }
    }

    request.unkE0.vx = action.unk844.vx;
    request.unkE0.vy = action.unk844.vy;
    request.unkE0.vz = action.unk844.vz;
    if (request.effectId == 0x27 and action.unk4C[0].unk40 == 4) {
        request.effectId = 0x24;
    }
    _ = c.func_800CF0E8(psx.cptr(c.func_800CF0E8_t, &request), mode, @as(c_int, action.unk2));

    if (action.actionIndex >= 0x8D and action.actionIndex < 0xE0) {
        // the caster is not bounded by the C
        const actor = psx.at(v.g.vs_battle_actors(), action.unk4.unk0.targetActor).*.?;
        object.dataType = 7;
        object.index = @bitCast(action.unk4.unk0.targetActor);
        object.modelId = 0;
        if (action.actionIndex < 0xB8) {
            object.actorId = 5;
            object.modelId = armorModel(actor);
        } else if (object.index != 0) {
            object.actorId = 5;
            object.modelId = armorModel(actor);
        } else {
            object.actorId = 4;
            object.modelId = psx.conv(i16, @rem(@as(c_int, action.actionIndex) - 0xB8, 4));
        }
        object.material = actor.equippedWeaponCategory;
        _ = c.vs_battle_populateDataSlot(@ptrCast(&object));
    }

    c.func_800CB654(1);
    if (v.g.vs_main_settings().information != 0) {
        c.vs_battle_displaySceneMessage(0xB, @as(c_int, action.actionIndex), 1);
    }
    v.g._cameraMode().* = 5;
}

/// The actor's armour model: armor[unkC's low half][unkC's high half].unk2_4 (indices unchecked, as in the C).
fn armorModel(actor: *v.vs_battle_actor) i16 {
    const halves: *align(4) const [2]u16 = @ptrCast(&actor.unkC);
    const stats = psx.get(actor.stats).?;
    const flat: *[24]v.vs_battle_actor_unk8C0 = @ptrCast(&stats.armor);
    const entry = psx.at(flat, @as(c_int, halves[0]) * 4 + halves[1]);
    return @as(i16, psx.getBits(entry, "unk2_4"));
}
