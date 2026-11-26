//! Loads an object's animation sequence into battle actor slots (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// A sequence block and a shared set as native_model reads them (pc_nm_seq_block_at, pc_nm_seq_shared_at: the one reading of the format).
fn seqBlock(at: usize) c.pc_nm_seq_block {
    var b: c.pc_nm_seq_block = undefined;
    c.pc_nm_seq_block_at(@ptrFromInt(at), &b);
    return b;
}
fn seqShared(at: usize) c.pc_nm_seq_shared {
    var set: c.pc_nm_seq_shared = undefined;
    c.pc_nm_seq_shared_at(@ptrFromInt(at), &set);
    return set;
}

/// orig: _loadSeq (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Loads the sequence data an object names into its actor slot, or into the shared slots, and resumes the effect loader when needed.
pub fn loadSeq(object: ?*v.vs_battle_objectData) callconv(.c) c_int {
    const state = v.g.D_800E8F2B().*;
    if (state == 1) return finishEtm(object);
    const obj = object.?;
    // The data the object names: a file's address, or (below 17) the slot of an actor that has it.
    var at: usize = obj.dataAddr;

    if (obj.actorId == 0) {
        const actor = psx.at(v.g.vs_actors_actorRecords(), obj.index).*.?;
        if (obj.modelId != 0) {
            if (findSharedSequence(obj)) at = obj.dataAddr;
        }
        if (obj.modelId == 0) {
            if (at < 17) {
                const source = psx.at(v.g.vs_actors_actorRecords(), @as(c_int, @intCast(at))).*.?;
                actor.unk5B4 = source.unk5B4;
                psx.put(&actor.unk5D4, psx.get(source.unk5D4));
                return 0;
            }
            const blk = seqBlock(at);
            actor.unk5B4 = blk.head;
            const size = roundUp4(blk.size);
            const alloc = c.vs_main_allocHeap(psx.conv(c_uint, size));
            if (alloc == null) return -2;
            const seq = seqOf(alloc);
            psx.put(&actor.unk5D4, seq);
            return copySequence(seq, size, blk.data);
        }
        if (obj.modelId == 0xFF) {
            const blk = seqBlock(at);
            const size = roundUp4(blk.size);
            const alloc = c.vs_main_allocHeap(psx.conv(c_uint, size));
            if (alloc == null) return -2;
            const seq = seqOf(alloc);
            psx.put(&actor.unk5D0, seq);
            return copySequence(seq, size, blk.data);
        }
        if (at < 17) {
            const source = psx.at(v.g.vs_actors_actorRecords(), @as(c_int, @intCast(at))).*.?;
            actor.unk5B5 = source.unk5B5;
            var seq = seqOf(psx.get(source.unk5D8));
            if (obj.index < 2) {
                var doClone = false;
                if (actor.unk0.slot == 0) {
                    seq = seqOf(v.g.D_800F4930().*);
                    doClone = true;
                } else if (at == 0) {
                    seq = seqOf(c.vs_main_allocHeap(0x7800));
                    doClone = true;
                }
                if (doClone) c.vs_content_copyWeaponSequence(cSeq(seq), cSeq(psx.get(source.unk5D8)));
            }
            psx.put(&actor.unk5D8, seq);
            actor.unk5B1 = psx.conv(u8, obj.modelId);
            return 0;
        }
        const blk = seqBlock(at);
        actor.unk5B5 = blk.head;
        if (blk.bones != 0 and @as(c_int, blk.bones) != @as(c_int, actor.unk0.nBones)) return 0;
        const size = roundUp4(blk.size);
        var seq: ?*align(1) v.func_8009C378_t = null;
        if (actor.unk0.slot != 0) {
            const alloc = c.vs_main_allocHeap(psx.conv(c_uint, size));
            if (alloc == null) return -2;
            seq = seqOf(alloc);
        } else {
            seq = seqOf(v.g.D_800F4930().*);
        }
        psx.put(&actor.unk5D8, seq);
        actor.unk5B1 = psx.conv(u8, obj.modelId);
        return copySequence(seq, size, blk.data);
    } else if (obj.actorId == 3) {
        const blk = seqBlock(at);
        const size = roundUp4(blk.size);
        return copySequence(seqOf(v.g.D_800F4768().*), size, blk.data);
    } else if (obj.actorId == 4 or obj.actorId == 5) {
        const actor = psx.at(v.g.vs_actors_actorRecords(), obj.index).*.?;
        const blk = seqBlock(at);
        actor.unk5B6 = blk.head;
        const size = roundUp4(blk.size);
        const alloc = c.vs_main_allocHeap(psx.conv(c_uint, size));
        if (alloc == null) return -2;
        const seq = seqOf(alloc);
        psx.put(&actor.unk5DC[3], seq);
        return copySequence(seq, size, blk.data);
    } else {
        const slot: c_int = @as(c_int, obj.index) -% 1;
        psx.at(v.g.D_800F2450(), slot).* = 0;
        if (psx.at(v.g.vs_actors_sharedSequenceData(), slot).* != null) return -2;
        psx.at(v.g.vs_actors_sharedSequenceData(), slot).* = @ptrFromInt(at);
        const set = seqShared(at);
        const count = set.count;
        var i: c_int = 0;
        while (i < count) : (i += 1) {
            psx.at(psx.at(v.g.vs_actors_sharedSequenceAnimationIds(), slot), i).* = psx.conv(i16, c.pc_nm_seq_shared_id(&set, i));
        }
        i = 0;
        while (i < count) : (i += 1) {
            psx.at(psx.at(v.g.D_800F4938(), slot), i).* = psx.conv(i16, c.pc_nm_seq_shared_key(&set, i));
        }
        psx.at(psx.at(v.g.vs_actors_sharedSequenceAnimationIds(), slot), i).* = -1;
        i = 0;
        while (i < count) : (i += 1) {
            psx.at(psx.at(v.g.vs_actors_sharedSequenceAnimationData(), slot), i).* = toPtr(@as(usize, c.pc_nm_seq_shared_offset(&set, i)) +% @intFromPtr(c.pc_nm_seq_shared_streams(&set)));
        }
        psx.at(psx.at(v.g.vs_actors_sharedSequenceAnimationData(), slot), i).* = @ptrFromInt(std.math.maxInt(usize));
        i = 0;
        while (i < 17) : (i += 1) {
            if (v.g.vs_actors_actorRecords().*[@intCast(i)]) |other| c.vs_content_bindSharedActorSequence(@ptrCast(other), slot);
        }
        if (obj.actorId == 1) return 0;
        v.g.D_800E8F2B().* = 1;
        return finishEtm(object);
    }
}

fn finishEtm(object: ?*v.vs_battle_objectData) c_int {
    if (c._loadEtm(@ptrCast(object)) == -1) return -1;
    v.g.D_800E8F2B().* = 0;
    return 0;
}

fn copySequence(seq: ?*align(1) v.func_8009C378_t, size: c_int, data: [*]const u8) c_int {
    const s = seq.?;
    c.vs_main_memcpy(@ptrCast(s), @constCast(data), psx.conv(c_uint, size));
    return 0;
}

fn roundUp4(size: c_int) c_int {
    var s = size;
    if ((s & 3) != 0) s +%= (4 -% s) & 3;
    return s;
}

/// Views pointer to the C struct pointer. Sequences live at 4-aligned guest
/// addresses (heap blocks and the shared slots), so the alignment assert holds.
fn cSeq(p: anytype) [*c]c.func_8009C378_t {
    return @ptrCast(@alignCast(p));
}

fn seqOf(p: ?*anyopaque) ?*align(1) v.func_8009C378_t {
    return @ptrCast(p);
}

fn toPtr(a: usize) ?*u8 {
    return @ptrFromInt(a);
}

/// orig: vs_battleFindSharedSequence (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Finds another actor slot holding the same sequence for this object and points the object at it.
fn findSharedSequence(object: *v.vs_battle_objectData) bool {
    const slots = v.g.vs_actors_actorRecords();
    const t1: u16 = psx.at(slots, object.index).*.?.unk6E6;
    var a: c_int = @as(c_int, @intFromBool(object.index >= 2)) * 2;
    while (a < 17) : (a += 1) {
        if (a == @as(c_int, object.index)) continue;
        const temp = slots[@intCast(a)] orelse continue;
        if (temp.unk6E6 != t1) continue;
        const shared = if (object.modelId == 0)
            psx.get(temp.unk5D4) != null
        else
            @as(c_int, temp.unk5B1) == @as(c_int, object.modelId);
        if (shared) {
            object.dataAddr = psx.conv(u32, a);
            return true;
        }
    }
    return false;
}
