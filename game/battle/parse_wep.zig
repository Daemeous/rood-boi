//! Parses a weapon model file (WEP) into its model slot, in two passes (src/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// The C calls memcpy and memset without a prototype: an int size goes as its 32 bits, zero-extended.
const Memcpy = *const fn (?*anyopaque, ?*const anyopaque, usize) callconv(.c) ?*anyopaque;
const Memset = *const fn (?*anyopaque, c_int, usize) callconv(.c) ?*anyopaque;

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

inline fn intBits(size: c_int) usize {
    return @as(u32, @bitCast(size));
}

/// orig: _parseWep (src/BATTLE/BATTLE.PRG/30DB0.c)
/// First pass (D_800E8F29 0): shares an identical model already loaded, or reads the header, the offsets block,
/// a free texture slot, the palettes and the texture (returns -1, D_800E8F29 1). Second pass (1): shifts the
/// polygons' UVs into the texture slot and copies the bone info. Returns 0 when done, -2/-3 on failure.
pub fn parseWep(object: *v.vs_battle_objectData) callconv(.c) c_int {
    const memcpy = @extern(Memcpy, .{ .name = "memcpy" });
    const memset = @extern(Memset, .{ .name = "memset" });
    const models = v.g.vs_battle_wepModels();
    var model = psx.at(models, object.index).*;
    var data: usize = object.dataAddr;
    var texV: c_int = undefined;

    const Next = enum { parse, copy_slot, fix_prims };
    var next: Next = .parse;
    const pass = v.g.D_800E8F29();
    if (pass.* == 0) {
        if (model == null) {
            psx.at(models, object.index).* = @ptrCast(@alignCast(psx.get(object.unk4)));
            model = psx.at(models, object.index).*;
            if (model == null) return -2;
            _ = memset(model, 0, @sizeOf(v.vs_battle_wepModels_t));
            model.?.texSlot = 0xFF;
        }
        if (model.?.nBones != 0) return -3;

        if (psx.addr(@as(?*u8, @ptrFromInt(data))) < 20) {
            next = .copy_slot;
        } else {
            var found = false;
            var j: c_int = 0;
            while (j < 20) : (j += 1) { // j < 20: the slot count
                if (j != object.index) {
                    if (models[@intCast(j)]) |other| {
                        if ((psx.getBits(other, "actorId") != 0 or object.index < 4) and
                            @as(c_int, other.modelId) == object.modelId)
                        {
                            object.dataAddr = @intCast(j);
                            found = true;
                            break;
                        }
                    }
                }
            }
            if (found) {
                data = object.dataAddr;
                next = .copy_slot;
            } else {
                model.?.modelId = psx.conv(u8, object.modelId);
            }
        }
    } else if (pass.* == 1) {
        data = @intFromPtr(v.g._wepFileEpilog().*);
        texV = 0xE0;
        next = .fix_prims;
    }

    switch (next) {
        .copy_slot => {
            const m = model.?;
            const slot: u32 = psx.addr(@as(?*u8, @ptrFromInt(data)));
            c.vs_main_memcpy(m, models[slot], @sizeOf(v.vs_battle_wepModels_t));
            if (m.texSlot != 0xFF) {
                const textures = v.g.vs_battle_wepTextures();
                psx.at(textures, m.texSlot).refCount +%= 1;
                if (psx.getBits(m, "isWideTexture") != 0) {
                    psx.at(textures, @as(c_int, m.texSlot) + 1).refCount +%= 1;
                }
            }
            if (object.index < 4) {
                data = @intFromPtr(psx.get(m.offsets));
                var buf: ?*v._wepModelOffsets = undefined;
                if (object.index < 2) {
                    buf = v.g.vs_battle_commonWepBuffs()[@intCast(object.index)];
                    psx.put(&m.offsets, buf);
                } else {
                    buf = @ptrCast(@alignCast(c.vs_main_allocHeap(@sizeOf(v._wepModelOffsets))));
                    psx.put(&m.offsets, buf);
                }
                c.vs_main_memcpy(buf, @ptrFromInt(data), @sizeOf(v._wepModelOffsets));
                const delta: u32 = psx.addr(buf) -% psx.addr(@as(?*u8, @ptrFromInt(data)));
                const b = buf.?;
                b.texturesOffset = @bitCast(@as(u32, @bitCast(b.texturesOffset)) +% delta);
                b.vertexGroupsOffset = @bitCast(@as(u32, @bitCast(b.vertexGroupsOffset)) +% delta);
                b.verticesOffset = @bitCast(@as(u32, @bitCast(b.verticesOffset)) +% delta);
                b.polygonsOffset = @bitCast(@as(u32, @bitCast(b.polygonsOffset)) +% delta);
            }
            m.clutSlotOffset = psx.conv(u8, @as(c_int, object.unk11) +% (@as(c_int, object.actorId) << 1) +% 16);
            c._loadMaterialPalette(object.index, object.material);
            m.unk11 = 64;
            psx.setBits(m, "unk8_4", 0);
            psx.setBits(m, "unk8_6", 0);
            m.unk13 = 0;
            psx.setBits(m, "actorId", psx.conv(u4, object.actorId));
            m.unkC = object.unk11;
            m.unkD = psx.conv(u8, c.func_800A152C(object.actorId, object.unk11, 0));
            pass.* = 0;
            return 0;
        },
        .fix_prims => {
            const m = model.?;
            const count: c_int = @as(c_int, m.nOther) + m.nTriangles + m.nQuads;
            var prim: usize = @as(u32, @bitCast(psx.get(m.offsets).?.polygonsOffset));
            const texU: c_int = (@as(c_int, m.texSlot) & 1) * 64;
            var n: c_int = 0;
            while (n < count) : (n += 1) { // n < count <= 3 * 0xFFFF
                var vertices: c_int = 4;
                if (byte(prim).* == c.primPolyFT3) {
                    prim +%= 10;
                    vertices = 3;
                } else {
                    prim +%= 12;
                }
                var k: c_int = 0;
                while (k < vertices) : ({
                    prim +%= 2;
                    k += 1; // k < 4
                }) {
                    byte(prim).* = psx.conv(u8, @as(c_int, byte(prim).*) +% texU);
                    byte(prim +% 1).* = psx.conv(u8, @as(c_int, byte(prim +% 1).*) +% texV);
                }
            }
            _ = memcpy(&m.unkBoneInfo, @ptrFromInt(data), intBits(@as(c_int, m.nBones) * 8));
            c._loadMaterialPalette(object.index, object.material);
            m.unk11 = 64;
            psx.setBits(m, "unk8_4", 0);
            psx.setBits(m, "unk8_6", 0);
            m.unk13 = 0;
            psx.setBits(m, "actorId", psx.conv(u4, object.actorId));
            m.unkC = object.unk11;
            m.unkD = psx.conv(u8, c.func_800A152C(object.actorId, object.unk11, 0));
            pass.* = 0;
            return 0;
        },
        .parse => {},
    }

    // A pass other than 0 and 1 parses into the slot as it is (null: the C crashes on it).
    const m = model.?;
    data = advance(data, 4);
    @as(*align(1) c_int, @ptrCast(&m.nBones)).* = readInt(data);
    @as(*align(1) c_int, @ptrCast(&m.nQuads)).* = readInt(data +% 4);
    data = advance(data, 8);
    var size = readInt(data);
    data = advance(data, 4);

    var buf: ?*v._wepModelOffsets = null;
    if (object.index == 0) {
        buf = v.g.vs_battle_commonWepBuffs()[0];
    } else if (object.index == 1) {
        buf = v.g.vs_battle_commonWepBuffs()[1];
    }
    var dst = buf;
    if (dst == null) dst = @ptrCast(@alignCast(c.vs_main_allocHeap(psx.conv(c_uint, size))));
    _ = memcpy(dst, @ptrFromInt(data), intBits(size));
    const base: u32 = psx.addr(dst);
    const d = dst.?;
    d.texturesOffset = @bitCast(@as(u32, @bitCast(d.texturesOffset)) +% base);
    d.vertexGroupsOffset = @bitCast(@as(u32, @bitCast(d.vertexGroupsOffset)) +% base);
    d.verticesOffset = @bitCast(@as(u32, @bitCast(d.verticesOffset)) +% base);
    d.polygonsOffset = @bitCast(@as(u32, @bitCast(d.polygonsOffset)) +% base);
    psx.put(&m.offsets, dst);
    if (dst == null) return -2;

    data = advance(data, size);
    data = advance(data, 5);
    var texW: c_uint = byte(data).*;
    texW *%= 2;
    data = advance(data, 2);
    size = byte(data).*;
    data = advance(data, 1);

    const textures = v.g.vs_battle_wepTextures();
    var i: c_int = 0;
    if (@as(c_int, @bitCast(texW)) < 65) {
        psx.setBits(m, "isWideTexture", 0);
        while (i < 20) : (i += 1) { // i < 20: the slot count
            if (textures[@intCast(i)].refCount == 0) {
                textures[@intCast(i)].refCount = 1;
                break;
            }
        }
        if (i == 20) return -2;
    } else {
        psx.setBits(m, "isWideTexture", 1);
        while (i < 20) : (i += 2) { // i < 20: the slot count
            if (textures[@intCast(i)].refCount == 0 and textures[@intCast(i + 1)].refCount == 0) {
                textures[@intCast(i)].refCount = 1;
                textures[@intCast(i + 1)].refCount = 1;
                break;
            }
        }
        if (i >= 20) return -2;
    }

    m.texSlot = psx.conv(u8, i);
    m.clutSlotOffset = psx.conv(u8, @as(c_int, object.unk11) +% (@as(c_int, object.actorId) << 1) +% 16);
    var w: c_int = undefined;
    var count: c_int = undefined;
    if (size == 96) {
        w = 32;
        count = 64;
        psx.setBits(m, "isDoubleClut", 1);
        m.nClutColors = 96;
    } else {
        w = 16;
        count = 32;
        psx.setBits(m, "isDoubleClut", 0);
        m.nClutColors = 48;
    }
    c.vs_main_memcpy(&m.unk440, @ptrFromInt(data), @intCast(w * 2));
    data = advance(data, w * 2);
    i = 0;
    while (i < 7) : (i += 1) { // i < 7: the palette count
        c.vs_main_memcpy(&m.palettes[@intCast(i)], @ptrFromInt(data), @intCast(count * 2));
        data = advance(data, count * 2);
    }

    const texU: c_int = @divTrunc(@as(c_int, m.texSlot), 2) * 64 + (@as(c_int, m.texSlot) & 1) * 32;
    texV = 0xE0;
    var rect: v.RECT = .{
        .x = psx.conv(i16, texU),
        .y = psx.conv(i16, texV),
        .w = psx.conv(i16, texW / 2),
        .h = 32,
    };
    // LoadImage's u_long* as the C passes it: a byte pointer, any alignment.
    const loadImage: *const fn ([*c]c.RECT, ?*const anyopaque) callconv(.c) c_int = @ptrCast(&c.LoadImage);
    _ = loadImage(psx.cptr(c.RECT, &rect), @ptrFromInt(data));
    data +%= @as(usize, texW *% 32);
    pass.* = 1;
    v.g._wepFileEpilog().* = @ptrFromInt(data);
    return -1;
}
