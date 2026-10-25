//! The sequence loader: stages an object's sequence data from disc, one step per call (src/BATTLE/BATTLE.PRG/30DB0.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// Address `base + delta` with a signed byte offset, wrapping as the guest's 32-bit arithmetic does.
fn offset(base: usize, delta: c_int) usize {
    return base +% @as(usize, @bitCast(@as(isize, delta)));
}

fn loadU16(addr: usize) u16 {
    const p: *align(1) const u16 = @ptrFromInt(addr);
    return p.*;
}

fn loadU32(addr: usize) u32 {
    const p: *align(1) const u32 = @ptrFromInt(addr);
    return p.*;
}

/// orig: vs_battleFindSharedSequence (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Looks for another loaded actor on the same sequence set and, if found, shares its data address.
fn findSharedSequence(object: *v.vs_battle_objectData) bool {
    const table = v.g.D_800F4538();
    const t1: c_int = @as(c_int, psx.at(table, object.index).*.?.unk6E6);
    var a2: c_int = if (object.index >= 2) 2 else 0;
    while (a2 < 17) : (a2 +%= 1) {
        if (a2 != @as(c_int, object.index)) {
            const temp = table[@intCast(a2)];
            if (temp) |t| {
                if (@as(c_int, t.unk6E6) == t1) {
                    const matches = (object.modelId == 0 and psx.get(t.unk5D4) != null) or
                        (object.modelId != 0 and @as(c_int, t.unk5B1) == @as(c_int, object.modelId));
                    if (matches) {
                        object.dataAddr = psx.conv(u32, a2);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/// orig: vs_battleQueueSequenceObject (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Copies an object into the first free object slot; returns the slot or -1 when none is free.
fn queueSequenceObject(objectData: *const v.vs_battle_objectData) c_int {
    const slots = psx.globalMany(v.vs_battle_objectData, "vs_battle_objectDataSlots");
    var slot: c_int = -1;
    var i: usize = 0;
    while (i < 16) : (i += 1) {
        if (slots[i].dataType == 0) {
            slot = @intCast(i);
            break;
        }
    }
    if (slot == -1) return -1;
    slots[@intCast(slot)] = objectData.*;
    return slot;
}

/// The shared "special" load: sequence set from the material and model, falling back to set 2.
/// Returns null to continue, or the failure value.
fn loadSpecial(object: *v.vs_battle_objectData, sectors: *c_uint) ?c_int {
    const row = psx.at(v.g.D_800E8EFC(), object.material);
    sectors.* = psx.at(row, object.modelId).*;
    if (sectors.* == 0) {
        sectors.* = psx.at(psx.at(v.g.D_800E8EFC(), @as(c_int, 2)), @as(c_int, 0)).*;
        object.material = 2;
        object.modelId = 0;
    }
    v.g.D_800F22C8().* = c.vs_main_allocHeapR(sectors.* << 11);
    object.dataAddr = psx.addr(v.g.D_800F22C8().*);
    if (v.g.D_800F22C8().* == null) return -2;
    const lba: c_int = @as(c_int, psx.at(psx.at(v.g.D_800E8EA8(), object.material), object.modelId).*);
    v.g.D_800F22D8().lba = psx.conv(i32, lba +% 0x1943E);
    return null;
}

/// Chooses the sequence to load for the object's actor kind; null continues to the disc request.
fn startLoad(object: *v.vs_battle_objectData, actor: ?*v.D_800F4538_t, sectors: *c_uint) ?c_int {
    switch (object.actorId) {
        0 => {
            const sequences: usize = @intFromPtr(actor) +% 0x5F4;
            sectors.* = @as(c_uint, loadU16(offset(sequences, 0xA0 +% @as(c_int, object.modelId) *% 2)) >> 11);
            if (sectors.* == 0) return 0;
            v.g.D_800F22C8().* = c.vs_main_allocHeapR(sectors.* << 11);
            object.dataAddr = psx.addr(v.g.D_800F22C8().*);
            if (v.g.D_800F22C8().* == null) return -2;
            v.g.D_800F22D8().lba = psx.conv(i32, loadU32(offset(sequences, 0x70 +% @as(c_int, object.modelId) *% 4)));
            if (object.index == 0) {
                const model = object.modelId;
                if (model != 0) {
                    // The original leaves the unset fields of `queued` uninitialised; they are zero here.
                    var queued = std.mem.zeroes(v.vs_battle_objectData);
                    queued.dataType = 7;
                    queued.index = 0;
                    queued.actorId = 3;
                    queued.material = psx.conv(u8, model);
                    _ = queueSequenceObject(&queued);
                }
            }
        },
        1, 2 => {
            sectors.* = psx.at(v.g.D_800E8B40(), object.modelId).*;
            if (sectors.* == 0) return -2;
            v.g.D_800F22C8().* = c.vs_main_allocHeap(sectors.* << 11);
            object.dataAddr = psx.addr(v.g.D_800F22C8().*);
            if (v.g.D_800F22C8().* == null) return -2;
            const lba: c_int = @as(c_int, psx.at(v.g.D_800E8A60(), object.modelId).*);
            v.g.D_800F22D8().lba = psx.conv(i32, lba +% 0x19797);
        },
        3 => {
            sectors.* = psx.at(v.g.D_800E8EA3(), object.material).*;
            v.g.D_800F22C8().* = c.vs_main_allocHeapR(sectors.* << 11);
            object.dataAddr = psx.addr(v.g.D_800F22C8().*);
            if (v.g.D_800F22C8().* == null) return -2;
            const lba: c_int = @as(c_int, psx.at(v.g.D_800E8E8E(), object.material).*);
            v.g.D_800F22D8().lba = psx.conv(i32, lba +% 0x18043);
        },
        4 => return loadSpecial(object, sectors),
        5 => {
            if (object.modelId == 0) return 0;
            object.modelId -%= 1;
            if (actor.?.unk6E6 == 2) return loadSpecial(object, sectors);
            const seq: usize = @intFromPtr(actor) +% 0x6CC;
            sectors.* = @as(c_uint, loadU16(offset(seq, @as(c_int, object.modelId) *% 2)) >> 11);
            if (sectors.* == 0) return -2;
            v.g.D_800F22D8().lba = psx.conv(i32, psx.at(&actor.?.spSeqLbas, object.modelId).*);
            v.g.D_800F22C8().* = c.vs_main_allocHeapR(sectors.* << 11);
            object.dataAddr = psx.addr(v.g.D_800F22C8().*);
            if (v.g.D_800F22C8().* == null) return -2;
        },
        // No other actor kind has a case: the original leaves `sectors` uninitialised here;
        // it is zero in this port.
        else => {},
    }
    return null;
}

/// Waits for the queued disc read: -1 while it is still loading, then frees the slot and moves on.
fn waitForLoad() c_int {
    const slot = v.g.D_800F22E0().*;
    if (slot.?.state != 4) return -1;
    c.vs_main_freeCdQueueSlot(psx.cptr(c.vs_main_CdQueueSlot, slot.?));
    v.g._loadShpState().* = 2;
    return -1;
}

/// Parses the loaded sequence and releases the staging buffer when this load owned it.
fn parseSequence(object: *v.vs_battle_objectData) c_int {
    const result = c._loadSeq(@ptrCast(object));
    if (result == -1) return -1;
    if (v.g.D_800F22D0().* != 0) {
        v.g.D_800F22D0().* = 0;
        c.vs_main_freeHeapR(v.g.D_800F22C8().*);
    }
    v.g._loadShpState().* = 0;
    return result;
}

/// orig: func_8009BE5C (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Runs one step of loading an object's sequence data; returns -1 while the disc read is pending.
pub fn func_8009BE5C(object_: ?*v.vs_battle_objectData) callconv(.c) c_int {
    const object = object_.?;
    const actor = psx.at(v.g.D_800F4538(), object.index).*;
    const state = v.g._loadShpState().*;

    if (state == 1) return waitForLoad();
    if (state == 2) return parseSequence(object);
    if (state == 0) {
        switch (object.actorId) {
            0 => {
                if (object.index != 0 and findSharedSequence(object)) {
                    v.g.D_800F22D0().* = 0;
                    return parseSequence(object);
                }
                v.g.D_800F22D0().* = 1;
            },
            1, 2 => {
                v.g.D_800F22D0().* = 0;
            },
            else => {
                v.g.D_800F22D0().* = 1;
            },
        }
    }

    var sectors: c_uint = 0;
    if (startLoad(object, actor, &sectors)) |result| return result;

    v.g.D_800F22D8().size = psx.conv(i32, sectors << 11);
    v.g._loadShpState().* = 1;
    const slot = c.vs_main_allocateCdQueueSlot(psx.cptr(c.vs_main_CdFile, v.g.D_800F22D8()));
    v.g.D_800F22E0().* = @ptrCast(slot);
    c.vs_main_cdEnqueue(slot, @ptrFromInt(@as(usize, object.dataAddr)));
    return waitForLoad();
}
