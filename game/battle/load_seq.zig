//! Loads an object's animation sequence into battle actor slots (src/BATTLE/BATTLE.PRG/30DB0.c).
const std = @import("std");
const common = @import("common");
const psx = common.psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// orig: _loadSeq (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Loads the sequence data an object names into its actor slot, or into the shared slots, and resumes the effect loader when needed.
pub fn loadSeq(object: ?*v.vs_battle_objectData) callconv(.c) c_int {
    const state = v.g.D_800E8F2B().*;
    if (state == 1) return finishEtm(object);
    const obj = object.?;
    if (state == 0) v.g.D_800F22E4().* = dataPtr(obj.dataAddr);

    if (obj.actorId == 0) {
        const actor = psx.at(v.g.D_800F4538(), obj.index).*.?;
        if (obj.modelId != 0) {
            if (findSharedSequence(obj)) v.g.D_800F22E4().* = dataPtr(obj.dataAddr);
        }
        if (obj.modelId == 0) {
            if (cursorAddr() < 17) {
                const source = psx.at(v.g.D_800F4538(), @as(c_int, @intCast(cursorAddr()))).*.?;
                actor.unk5B4 = source.unk5B4;
                psx.put(&actor.unk5D4, psx.get(source.unk5D4));
                return 0;
            }
            actor.unk5B4 = readU8();
            advance(4);
            var size = readI32();
            advance(4);
            size = roundUp4(size);
            const alloc = c.vs_main_allocHeap(psx.conv(c_uint, size));
            if (alloc == null) return -2;
            const seq = seqOf(alloc);
            psx.put(&actor.unk5D4, seq);
            return copySequence(seq, size);
        }
        if (obj.modelId == 0xFF) {
            advance(4);
            var size = readI32();
            advance(4);
            size = roundUp4(size);
            const alloc = c.vs_main_allocHeap(psx.conv(c_uint, size));
            if (alloc == null) return -2;
            const seq = seqOf(alloc);
            psx.put(&actor.unk5D0, seq);
            return copySequence(seq, size);
        }
        if (cursorAddr() < 17) {
            const source = psx.at(v.g.D_800F4538(), @as(c_int, @intCast(cursorAddr()))).*.?;
            actor.unk5B5 = source.unk5B5;
            var seq = seqOf(psx.get(source.unk5D8));
            if (obj.index < 2) {
                var doClone = false;
                if (actor.unk0.unkF == 0) {
                    seq = seqOf(v.g.D_800F4930().*);
                    doClone = true;
                } else if (cursor() == null) {
                    seq = seqOf(c.vs_main_allocHeap(0x7800));
                    doClone = true;
                }
                if (doClone) c.func_8009C378(cSeq(seq), cSeq(psx.get(source.unk5D8)));
            }
            psx.put(&actor.unk5D8, seq);
            actor.unk5B1 = psx.conv(u8, obj.modelId);
            return 0;
        }
        actor.unk5B5 = readU8();
        advance(2);
        var size: c_int = readU16();
        advance(2);
        if (size != 0 and size != @as(c_int, actor.unk0.nBones)) return 0;
        size = readI32();
        advance(4);
        size = roundUp4(size);
        var seq: ?*align(1) v.func_8009C378_t = null;
        if (actor.unk0.unkF != 0) {
            const alloc = c.vs_main_allocHeap(psx.conv(c_uint, size));
            if (alloc == null) return -2;
            seq = seqOf(alloc);
        } else {
            seq = seqOf(v.g.D_800F4930().*);
        }
        psx.put(&actor.unk5D8, seq);
        actor.unk5B1 = psx.conv(u8, obj.modelId);
        return copySequence(seq, size);
    } else if (obj.actorId == 3) {
        advance(4);
        var size = readI32();
        advance(4);
        size = roundUp4(size);
        return copySequence(seqOf(v.g.D_800F4768().*), size);
    } else if (obj.actorId == 4 or obj.actorId == 5) {
        const actor = psx.at(v.g.D_800F4538(), obj.index).*.?;
        actor.unk5B6 = readU8();
        advance(4);
        var size = readI32();
        advance(4);
        size = roundUp4(size);
        const alloc = c.vs_main_allocHeap(psx.conv(c_uint, size));
        if (alloc == null) return -2;
        const seq = seqOf(alloc);
        psx.put(&actor.unk5DC[3], seq);
        return copySequence(seq, size);
    } else {
        const slot: c_int = @as(c_int, obj.index) -% 1;
        psx.at(v.g.D_800F2450(), slot).* = 0;
        if (psx.at(v.g.D_800F4528(), slot).* != null) return -2;
        psx.at(v.g.D_800F4528(), slot).* = @ptrCast(cursor());
        const count = readI32();
        advance(4);
        var i: c_int = 0;
        while (i < count) : (i += 1) {
            psx.at(psx.at(v.g.D_800F4628(), slot), i).* = psx.conv(i16, readU16());
            advance(4);
        }
        i = 0;
        while (i < count) : (i += 1) {
            psx.at(psx.at(v.g.D_800F4938(), slot), i).* = psx.conv(i16, readU16());
            advance(4);
        }
        psx.at(psx.at(v.g.D_800F4628(), slot), i).* = -1;
        i = 0;
        while (i < count) : (i += 1) {
            psx.at(psx.at(v.g.D_800F4810(), slot), i).* = toPtr(readU32());
            advance(4);
        }
        i = 0;
        while (i < count) : (i += 1) {
            const entry = psx.at(psx.at(v.g.D_800F4810(), slot), i);
            entry.* = toPtr(guest(entry.*) +% cursorAddr());
            const sq: u32 = guest(entry.*) +% 8;
            const seq: *align(1) v.func_8009C378_t = @ptrFromInt(@as(usize, sq));
            rebase(&seq.unk0, sq);
            rebase(&seq.unk4, sq);
        }
        psx.at(psx.at(v.g.D_800F4810(), slot), i).* = @ptrFromInt(std.math.maxInt(usize));
        i = 0;
        while (i < 17) : (i += 1) {
            if (v.g.D_800F4538().*[@intCast(i)]) |other| c.func_8009CAEC(@ptrCast(other), slot);
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

fn copySequence(seq: ?*align(1) v.func_8009C378_t, size: c_int) c_int {
    const s = seq.?;
    c.vs_main_memcpy(@ptrCast(s), @ptrCast(cursor().?), psx.conv(c_uint, size));
    rebase(&s.unk0, psx.addr(s));
    rebase(&s.unk4, psx.addr(s));
    return 0;
}

/// psx_set(field, (char*)psx_get(field) + base): the field's guest address moved by `base`.
fn rebase(field: anytype, base: u32) void {
    const moved: usize = psx.rawOf(field.*) +% base;
    psx.put(field, @as(?*anyopaque, @ptrFromInt(moved)));
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

fn dataPtr(addr: u32) ?[*]u8 {
    // psx_to_ptr: the integer zero-extended; address 0 is NULL, which the cursor may hold.
    return @ptrFromInt(@as(usize, addr));
}

fn toPtr(a: u32) ?*u8 {
    return @ptrFromInt(@as(usize, a));
}

fn guest(p: ?*u8) u32 {
    const q = p orelse return 0;
    return @truncate(@intFromPtr(q));
}

fn cursor() ?[*]u8 {
    return v.g.D_800F22E4().*;
}

fn cursorAddr() u32 {
    const p = cursor() orelse return 0;
    return psx.addr(p);
}

fn readU8() u8 {
    return cursor().?[0];
}

fn readU16() u16 {
    return @as(*align(1) const u16, @ptrCast(cursor().?)).*;
}

fn readU32() u32 {
    return @as(*align(1) const u32, @ptrCast(cursor().?)).*;
}

fn readI32() c_int {
    return @as(*align(1) const c_int, @ptrCast(cursor().?)).*;
}

fn advance(n: usize) void {
    const p = v.g.D_800F22E4();
    p.* = p.*.? + n;
}

/// orig: vs_battleFindSharedSequence (src/BATTLE/BATTLE.PRG/30DB0.c)
/// Finds another actor slot holding the same sequence for this object and points the object at it.
fn findSharedSequence(object: *v.vs_battle_objectData) bool {
    const slots = v.g.D_800F4538();
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
