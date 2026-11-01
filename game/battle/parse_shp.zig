//! Parses an actor's shape file (SHP) into its actor slot, in two passes (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// The C calls memcpy and memset without a prototype: an int size goes as its 32 bits, zero-extended.
const Memcpy = *const fn (?*anyopaque, ?*const anyopaque, usize) callconv(.c) ?*anyopaque;
const Memset = *const fn (?*anyopaque, c_int, usize) callconv(.c) ?*anyopaque;
/// func_8009D468 as the C calls it (2842C.h's `void func_8009D468();`): one argument.
const Reset = *const fn (c_int) callconv(.c) void;
/// LoadImage's u_long* as the C passes it: a byte pointer, any alignment.
const LoadImage = *const fn ([*c]c.RECT, ?*const anyopaque) callconv(.c) c_int;

/// The guest pointer `data` advanced by a C int (pointer arithmetic: sign-extended).
inline fn advance(data: usize, n: c_int) usize {
    return data +% @as(usize, @bitCast(@as(isize, n)));
}

inline fn readInt(data: usize) c_int {
    return @as(*align(1) const c_int, @ptrFromInt(data)).*;
}

inline fn byte(data: usize) *u8 {
    return @ptrFromInt(data);
}

/// *(u_int*)((char*)actor + 0x5AC): the actor's flag word.
inline fn flagWord(actor: *v.D_800F4538_t) c_uint {
    return psx.byteAt(c_uint, actor, 0x5AC).*;
}

/// A texture slot's VRAM origin from D_800E8F30 (x, y in 4-byte entries) and D_800E8F90 (the y step).
fn slotRect(slot: u8, rect: *v.RECT) void {
    const table = v.g.D_800E8F30();
    const x: c_int = @as(c_int, psx.byteAt(i16, table, @as(isize, slot) * 4).*) << 6;
    rect.x = psx.conv(i16, x);
    const y: c_int = (@as(c_int, psx.byteAt(i16, table, @as(isize, slot) * 4 + 2).*) << 8) +
        (@as(c_int, v.g.D_800E8F90()[slot]) << 6);
    rect.y = psx.conv(i16, y);
    rect.w = psx.conv(i16, @divTrunc(v.g.D_800F22C0().*, 2));
    rect.h = psx.conv(i16, v.g.D_800F22C4().*);
}

/// orig: _parseShp (src/BATTLE/BATTLE.PRG/30DB0.c)
/// First pass (D_800E8F29 0): Ashley's palette reload (index 0, model 6), the empty model 0x7F, a copy of an
/// actor sharing the model, or the header, the model (relocated), its SEQ, the CLUTs and a texture slot
/// (returns -1, D_800E8F29 1). Second pass (1): shifts the polygons' v into the slot. Then the palettes and the
/// actor's initial state. Returns 0 when done, -2 on failure.
pub fn parseShp(object: *v.vs_battle_objectData) callconv(.c) c_int {
    const memcpy = @extern(Memcpy, .{ .name = "memcpy" });
    const memset = @extern(Memset, .{ .name = "memset" });
    const reset: Reset = @ptrCast(&c.func_8009D468);
    const loadImage: LoadImage = @ptrCast(&c.LoadImage);
    const actors = v.g.D_800F4538();
    const textures = v.g.D_800F46A8();
    const pass = v.g.D_800E8F29();
    const clutCount = v.g.D_800F22B8();
    var rect: v.RECT = undefined;

    const Stage = enum { parse, copy_shared, fix_uvs, setup_palettes };
    var stage: Stage = .parse;
    // Uninitialised in the original when the pass is neither 0 nor 1 (a pass the loader never sets): null here.
    var actor_opt: ?*v.D_800F4538_t = null;
    var data: usize = 0;

    if (pass.* == 0) {
        if (object.index == 0 and object.modelId == 6) {
            data = object.dataAddr;
            const a = actors[0].?;
            data = advance(data, 0xF4);
            const size = readInt(data);
            data = advance(data, size +% 4);
            data = advance(data, 4);
            const skip = readInt(data);
            clutCount.* = a.unk0.unkE;
            v.g.D_800F22C0().* = 0x80;
            v.g.D_800F22C4().* = 0x80;
            data = advance(data, skip +% 4);
            data = advance(data, 8);
            c.vs_main_memcpy(&a.unk11A4, @ptrFromInt(data), @as(c_uint, clutCount.*) * 4);
            slotRect(a.unk5BB, &rect);
            _ = loadImage(psx.cptr(c.RECT, &rect), @ptrFromInt(advance(data, @as(c_int, clutCount.*) * 4)));
            object.variant = 0;
            actor_opt = a;
            stage = .setup_palettes;
        } else {
            const entry = psx.at(actors, object.index);
            if (entry.* == null) {
                entry.* = @ptrCast(@alignCast(psx.get(object.unk4)));
                if (entry.* == null) return -2;
                _ = memset(entry.*, 0, 0x1900);
                entry.*.?.unk5BB = 0xFF;
                entry.*.?.unk0.unk12 = 0xFF;
            }
            const a = entry.*.?;
            actor_opt = a;
            const modelId: c_int = object.modelId;
            if (modelId == 0x7F) {
                a.unk6E6 = psx.conv(u16, modelId);
                a.unk0.unkF = psx.conv(u8, object.index);
                psx.byteAt(u16, &a.unk640, 0).* = 0x40;
                psx.byteAt(u16, &a.unk640, 2).* = 10000;
                reset(a.unk0.unkF);
                psx.setBits(&a.unk0, "skip", 0);
                c.func_800A30A0(a.unk0.unkF, @ptrCast(&object.unkC), 0, 1);
                pass.* = 0;
                return 0;
            }
            if (object.dataAddr < 17 or c.func_8009AC24(@ptrCast(object)) != 0) {
                stage = .copy_shared;
            } else {
                a.unk6E6 = psx.conv(u16, object.modelId);
                data = object.dataAddr;
            }
        }
    } else if (pass.* == 1) {
        actor_opt = psx.at(actors, object.index).*;
        stage = .fix_uvs;
    }
    const actor = actor_opt.?;

    if (stage == .copy_shared) {
        c.vs_main_memcpy(actor, psx.at(actors, object.dataAddr).*, 0x1900);
        if (psx.getBits(actor, "unk5B0_5") != 0 and actor.unk5BB != 0xFF) {
            var i: c_int = 0;
            while (i < @as(c_int, psx.getBits(&actor.unk0, "unk8_4")) + 1) : (i += 1) { // i <= 7
                psx.at(textures, @as(c_int, actor.unk5BB) + i).refCount +%= 1;
            }
        }
        psx.byteAt(v.vs_battle_objectData_flags, &actor.unk0.currentTileX, 0).* = object.unkC;
        clutCount.* = if (flagWord(actor) & 0x08000000 != 0) 0xA0 else 0x10;
        stage = .setup_palettes;
    }

    if (stage == .parse) {
        data = advance(data, 4);
        psx.byteAt(v.vs_battle_objectData_flags, &actor.unk0.currentTileX, 0).* = object.unkC;
        psx.setBits(&actor.unk0, "skip", 1);
        psx.byteAt(c_int, &actor.unk0.nBones, 0).* = readInt(data);
        psx.byteAt(c_int, &actor.unk0.nQuads, 0).* = readInt(data +% 4);
        data = advance(data, 8);
        c.vs_main_memcpy(&actor.texMapOverlays, @ptrFromInt(data), 0xE8);
        data = advance(data, 0xE8);
        var size = readInt(data);
        data = advance(data, 4);
        const model: ?*v.D_800F4538_unk68 = @ptrCast(@alignCast(c.vs_main_allocHeap(psx.conv(c_uint, size))));
        _ = memcpy(model, @ptrFromInt(data), @as(u32, @bitCast(size)));
        const m = model.?;
        const base: usize = psx.addr(model);
        psx.put(&m.akaoOffset, @as(?*anyopaque, @ptrFromInt(@intFromPtr(psx.get(m.akaoOffset)) +% base)));
        psx.put(&m.groupOffset, @as(?*anyopaque, @ptrFromInt(@intFromPtr(psx.get(m.groupOffset)) +% base)));
        psx.put(&m.vertexOffset, @as(?*anyopaque, @ptrFromInt(@intFromPtr(psx.get(m.vertexOffset)) +% base)));
        psx.put(&m.polyOffset, @as(?*anyopaque, @ptrFromInt(@intFromPtr(psx.get(m.polyOffset)) +% base)));
        psx.put(&actor.unk0.unk68, model);
        if (model == null) return -2;
        data = advance(data, size);

        var queued: v.vs_battle_objectData = undefined;
        queued.dataType = 8;
        queued.index = object.index;
        queued.dataAddr = psx.addr(@as(?*u8, @ptrFromInt(data)));
        queued.modelId = 0xFF;
        queued.actorId = 0;
        _ = c._loadSeq(@ptrCast(&queued));
        data = advance(data, 4);

        var i: c_int = readInt(data);
        data = advance(data, 4);
        data = advance(data, i);
        size = readInt(data);
        psx.setBits(actor, "unk5B0_5", 1);
        data = advance(data, 4);

        if (size == 4) {
            size = readInt(data);
            var other: ?*v.D_800F4538_t = null;
            i = 0;
            while (i < 17) : (i += 1) { // i < 17: the actor slots
                other = actors[@intCast(i)];
                if (other != null and @as(c_int, other.?.unk6E6) == size) break;
            }
            const o = other.?;
            actor.unk0.unkE = o.unk0.unkE;
            clutCount.* = actor.unk0.unkE;
            c.vs_main_memcpy(&actor.unk11A4, &o.unk11A4, @as(c_uint, clutCount.*) * 4);
            actor.unk5BB = o.unk5BB;
            psx.setBits(&actor.unk0, "unk8_3", psx.getBits(&o.unk0, "unk8_3"));
            psx.setBits(actor, "unk5B0_5", 0);
            psx.setBits(actor, "unk5AC_27", 1);
            stage = .fix_uvs;
        } else {
            const header = v.g.D_800F22BC();
            header.* = byte(data).*;
            psx.setBits(actor, "unk5AC_27", psx.conv(u1, @as(c_int, psx.conv(u8, header.*))));
            psx.setBits(&actor.unk0, "unk8_3", psx.conv(u1, header.* >> 4));
            data +%= 1;
            v.g.D_800F22C0().* = @as(c_int, byte(data).*) * 2;
            data +%= 1;
            v.g.D_800F22C4().* = @as(c_int, byte(data).*) * 2;
            data +%= 1;
            clutCount.* = byte(data).*;
            actor.unk0.unkE = clutCount.*;
            data +%= 1;

            var n: c_int = @divTrunc(v.g.D_800F22C4().*, 64);
            if (v.g.D_800F22C0().* == 0x100) n = 8;
            psx.setBits(&actor.unk0, "unk8_4", psx.conv(u3, n -% 1));

            var lo: c_int = undefined;
            var hi: c_int = undefined;
            if (object.index == 1 or object.index == 16) {
                lo = 12;
                hi = 20;
            } else {
                lo = @as(c_int, @intFromBool(n >= 3)) * 4;
                hi = 12;
            }

            const Target = enum { found, fallback, all_free };
            var target: Target = .found;
            switch (n) {
                1, 2 => {
                    i = lo;
                    while (i < hi) : (i += n) {
                        if (psx.at(textures, i).refCount == 0) break;
                    }
                    if (i == hi) target = .fallback;
                },
                3, 4 => {
                    target = .fallback;
                    i = lo;
                    search: while (i < hi) : (i += 1) {
                        var j: c_int = 0;
                        while (j < n) : (j += 1) {
                            if (psx.at(textures, i + j).refCount != 0) break;
                        }
                        if (j == n) {
                            target = .found;
                            break :search;
                        }
                        if ((i + n) & 3 == 0) i += n - 1;
                    }
                },
                8 => {
                    i = lo;
                    while (i < hi) : (i += 1) {
                        if (psx.at(textures, i).refCount != 0) break;
                    }
                    target = if (i == hi) .all_free else .fallback;
                },
                // n 0, 5..7: no case; i keeps the skip length read above.
                else => {},
            }
            if (target == .fallback) {
                if (c.func_8007DFF0(31, 1, 4) == 0) {
                    actor.unk5BB = 0;
                    psx.setBits(&actor.unk0, "unk8_4", 0);
                    textures[0].refCount +%= 1;
                    stage = .setup_palettes;
                } else {
                    i = 20;
                }
            } else if (target == .all_free) {
                i = lo;
            }

            if (stage == .parse) {
                var j: c_int = 0;
                while (j < n) : (j += 1) {
                    psx.at(textures, i +% j).refCount = 1;
                }
                actor.unk5BB = psx.conv(u8, i);
                const slot = psx.at(textures, actor.unk5BB);
                if (flagWord(actor) & 0x08000000 != 0) {
                    slot.tpage = psx.conv(i16, @as(c_int, slot.tpage) | 0x80);
                } else {
                    slot.tpage = psx.conv(i16, @as(c_int, slot.tpage) & ~@as(c_int, 0x180));
                }
                c.vs_main_memcpy(&actor.unk11A4, @ptrFromInt(data), @as(c_uint, clutCount.*) * 4);
                data = advance(data, @as(c_int, clutCount.*) * 4);
                slotRect(actor.unk5BB, &rect);
                _ = loadImage(psx.cptr(c.RECT, &rect), @ptrFromInt(data));
                data = advance(data, v.g.D_800F22C0().* *% v.g.D_800F22C4().*);
                pass.* = 1;
                v.g.D_800F22B4().* = @ptrFromInt(data);
                return -1;
            }
        }
    }

    if (stage == .fix_uvs) {
        const count: c_int = @as(c_int, actor.unk0.nOther) + actor.unk0.nTriangles + actor.unk0.nQuads;
        var prim: usize = @intFromPtr(psx.get(psx.get(actor.unk0.unk68).?.polyOffset));
        const texV: c_int = @as(c_int, v.g.D_800E8F90()[actor.unk5BB]) << 6;
        if (psx.getBits(&actor.unk0, "unk8_3") == 0) {
            var k: c_int = 0;
            while (k < count) : (k += 1) {
                var vertices: c_int = 4;
                if (byte(prim).* == c.primPolyFT3) {
                    prim +%= 10;
                    vertices = 3;
                } else {
                    prim +%= 12;
                }
                var corner: c_int = 0;
                while (corner < vertices) : ({
                    prim +%= 2;
                    corner += 1;
                }) {
                    byte(prim).* +%= 0;
                    byte(prim +% 1).* = psx.conv(u8, @as(c_int, byte(prim +% 1).*) +% texV);
                }
            }
        } else {
            var k: c_int = 0;
            while (k < count) : (k += 1) {
                var vertices: c_int = undefined;
                if (byte(prim +% 11).* == 0x34) {
                    byte(prim +% 6).* +%= 0;
                    byte(prim +% 7).* = psx.conv(u8, @as(c_int, byte(prim +% 7).*) +% texV);
                    prim +%= 20;
                    vertices = 2;
                } else {
                    prim +%= 24;
                    vertices = 4;
                }
                var corner: c_int = 0;
                while (corner < vertices) : ({
                    prim +%= 2;
                    corner += 1;
                }) {
                    byte(prim).* +%= 0;
                    byte(prim +% 1).* = psx.conv(u8, @as(c_int, byte(prim +% 1).*) +% texV);
                }
            }
        }
    }

    // setupPalettes
    const palettes = &actor.unk1424;
    const colors: c_uint = @as(c_uint, clutCount.*) * 2;
    c.vs_main_memcpy(palettes, psx.at(&actor.unk11A4, @as(c_int, object.variant) * 0x140), colors);
    c.vs_main_memcpy(psx.at(palettes, 0x140), palettes, colors);
    c.vs_main_memcpy(psx.at(palettes, 0x280), palettes, colors);
    if (object.index == 16) {
        c.vs_main_loadClut(@ptrCast(@alignCast(palettes)), 0x17, 0, clutCount.*);
        actor.unk6E8 = textures[1].clut1;
        actor.unk6EA = textures[1].clut1;
    } else {
        c.vs_main_loadClut(@ptrCast(@alignCast(palettes)), psx.conv(c_uint, @as(c_int, object.index) + 4), 0, clutCount.*);
        c.vs_main_loadClut(@ptrCast(@alignCast(palettes)), psx.conv(c_uint, @as(c_int, object.index) + 0x16), 0, clutCount.*);
        actor.unk6E8 = psx.at(textures, object.index).clut0;
        actor.unk6EA = psx.at(textures, object.index).clut1;
    }
    if (object.index == 0 and object.modelId == 6) return 0;

    actor.unk0.unkF = psx.conv(u8, object.index);
    actor.unk0.unk10 = object.variant;
    psx.setBits(&actor.unk0, "unk8_7", 1);
    psx.setBits(&actor.unk0, "unk9_0", 0);
    actor.unk6E5 = 13;
    psx.put(&actor.unk5D4, null);
    psx.put(&actor.unk5D8, null);
    psx.put(&actor.unk5DC[3], null);
    actor.unk5B4 = 0;
    actor.unk5B5 = 0;
    actor.unk5B6 = 0;
    actor.unk5B3 = 0;
    actor.unk5B1 = 0;
    actor.unk5BC = 0;
    actor.unk6DC = 0;
    psx.setBits(&actor.unk0, "weaponDrawn", 0);
    psx.setBits(actor, "unk5AC_18", 0);
    psx.setBits(actor, "unk5AC_8", 0);
    actor.unk5C2 = 8;
    actor.unk5C0 = 0x20;
    actor.unk0.unk2C = 0x1000;
    actor.unk0.unk2E = 0x1000;
    actor.unk0.unk30 = 0x1000;
    actor.unk5B2 = 1;
    psx.setBits(&actor.unk0, "unkC_0", 0);
    psx.setBits(&actor.unk0, "unkC_4", 0);
    psx.setBits(actor, "unk5AC_11", 0);
    psx.setBits(&actor.unk0, "unk9_7", 1);
    psx.byteAt(c_int, &actor.unk6F0, 0).* = 0;
    psx.setBits(actor, "unk648_0", 800);
    actor.unk0.unk54 = 0x808080;
    psx.byteAt(c_int, &actor.unk0.unk58, 0).* = 0x404040;
    actor.unk183C = psx.conv(i16, v.g.D_800F4438().vx);

    var i: c_int = 0;
    while (i < actor.unk0.nBones) : (i += 1) {
        const armature = psx.at(&psx.get(actor.unk0.unk68).?.armatures, i);
        if (armature.unk8 >= 3 and armature.unk8 < 7) {
            const joint = psx.byteAt([2]u16, &actor.unk1880, (@as(isize, armature.unk8) - 3) * 4);
            joint[1] = 0;
            joint[0] = psx.byteAt(u16, &armature.unkA, 0).*;
        }
    }
    i = 0;
    while (i < 4) : (i += 1) {
        const state = psx.at(&actor.unk181C, i * 8);
        const bounds = psx.at(&actor.unk614, i * 8 + 8);
        const s: [*]u8 = @ptrCast(state);
        const b: [*]const u8 = @ptrCast(bounds);
        s[0] = b[0];
        s[1] = b[1];
        s[2] = b[4];
        s[3] = b[5];
        s[4] = b[7];
        s[5] = 0;
    }
    psx.put(&actor.unk5DC[0], null);
    psx.put(&actor.unk5DC[1], null);
    const effects = v.g.D_800F4528();
    i = 0;
    while (i < 3) : (i += 1) {
        if (effects[@intCast(i)] != null) c.func_8009CAEC(@ptrCast(actor), i);
    }
    c.func_8009D7E8(object.index, 0);
    reset(actor.unk0.unkF);
    actor.unk0.unk13 = 0;
    psx.setBits(&actor.unk0, "unk9_5", 0);

    if (object.actorId == 0xFF) {
        c.func_800A30A0(object.index, @ptrCast(&object.unkC), 0, object.material);
        actor.unk0.unk12 = 0xFF;
    } else if (object.unk11 == 0xFC) {
        actor.unk0.unk12 = object.actorId;
        actor.unk0.unk13 = 0xFC;
        psx.at(actors, actor.unk0.unk12).*.?.unk17FD = psx.conv(u8, object.index);
        psx.at(actors, actor.unk0.unk12).*.?.unk6F4.unk0 = 0;
        psx.at(actors, actor.unk0.unk12).*.?.unk6F4.unk1 = 0;
        psx.at(actors, actor.unk0.unk12).*.?.unk6F4.unk2 = 0;
        psx.at(actors, actor.unk0.unk12).*.?.unk6F4.unk3 = 8;
    } else {
        actor.unk0.unk12 = 0xFF;
        c.func_8009FF08(object.index, object.actorId, object.unk11, 0);
    }
    pass.* = 0;
    return 0;
}
