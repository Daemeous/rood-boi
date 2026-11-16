//! Battle object setup: fills an object's data block from a matching object or from its definition
//! (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c).
const psx = @import("common").psx;
const v = @import("views_30DB0");
const c = @import("c_30DB0");

/// The object actor's data: the header, eight bone records and three words (vs_battle_actor_dat2 from unk9B4).
const object_bytes: usize = @offsetOf(v.D_800F45E0_t, "unk6C") + 8 * @sizeOf(v.D_800F4538_t2) + 12;

/// orig: vs_field_createGimmickInstance (pc/src/game/BATTLE/BATTLE.PRG/30DB0.c)
/// Sets up battle object `arg0`'s data block: copies a matching object's block, or builds its own.
pub fn vs_field_createGimmickInstance(arg0: ?*v.vs_battle_objectData) callconv(.c) c_int {
    const obj = arg0.?;
    const t = v.g.vs_field_gimmickInstances();

    if (psx.at(t, obj.index).* != null) return -3;

    const raw = psx.get(obj.unk4);
    const fetched: ?*v.D_800F45E0_t = if (raw) |p| @ptrCast(@alignCast(p)) else null;
    psx.at(t, obj.index).* = fetched;
    const s1 = fetched orelse return -2;

    _ = c.memset(@as(?*anyopaque, @ptrCast(s1)), @as(c_int, 0), object_bytes);

    var found = false;
    var i: c_int = 0;
    while (i < 16) : (i += 1) {
        if (i != @as(c_int, obj.index)) {
            if (t[@intCast(i)]) |e| {
                if (@as(c_int, psx.getBits(&e.unk6C[8], "actorId")) == @as(c_int, obj.actorId)) {
                    obj.dataAddr = psx.conv(u32, i);
                    found = true;
                    break;
                }
            }
        }
    }

    if (found) {
        c.vs_main_memcpy(@ptrCast(s1), @ptrCast(psx.at(t, obj.dataAddr).*), @intCast(object_bytes));
    } else {
        const idx = v.g.vs_field_gimmickTemplateIndices()[obj.actorId];
        const ent: *v.D_800E8FB4_t = @ptrCast(@alignCast(psx.at(v.g.vs_field_gimmickModelTemplates(), idx).*.?));
        psx.fieldAt(i32, &s1.unk0, 0).* = ent.unk0;
        s1.unk4 = ent.unk4;
        const temp_s0: c_int = ent.unk8;
        const after: *anyopaque = @ptrCast(@as([*]v.D_800E8FB4_t, @ptrCast(ent)) + 1);
        psx.put(&s1.unk68, c.vs_content_copyModelGeometry(null, after, ent.unkC));

        if (psx.get(s1.unk68) == null) return -2;
        const header = psx.get(s1.unk68).?;
        const temp_v1_2: ?*anyopaque = @ptrFromInt(@intFromPtr(header) +% header.unk3C);

        var j: c_int = obj.actorId;
        if (j >= 6) {
            j = 5;
        }

        c.vs_content_offsetModelTextureCoordinates(
            temp_v1_2,
            temp_s0,
            1,
            @as(c_int, v.g.D_800E8FA8()[@intCast(j * 2)]),
            @as(c_int, v.g.D_800E8FA8()[@intCast(j * 2 + 1)]),
        );
    }

    const var_s0 = &s1.unk6C[6];

    s1.unkF = psx.conv(u8, obj.index);
    psx.setBits(&s1.unk6C[8], "actorId", psx.conv(u3, obj.actorId));

    const src = v.g.vs_field_gimmickPaletteColors() + @as(usize, psx.getBits(&s1.unk6C[8], "actorId")) *% 32;
    c.vs_main_memcpy(@ptrCast(var_s0), @ptrCast(src), @sizeOf(v.D_800F4538_t2));
    c.vs_main_loadClut(@ptrCast(var_s0), 0xD, @as(c_uint, s1.unkF) *% 0x10, 0x10);

    s1.unk6C[8].unk3 = 0xFF;
    s1.unk12 = 0xFF;

    if (psx.getBits(&s1.unk6C[8], "actorId") == 1) {
        s1.unk6C[8].unk1 = obj.unk11;
    }

    s1.unk54 = 0x808080;
    s1.unk2C = 0x40;
    s1.unk2E = 0x40;
    s1.unk30 = 0x40;
    s1.unk1A = 0;
    s1.unk64.unk0 = 0;
    psx.setBits(s1, "unk8_0", 1);
    psx.setBits(s1, "unk8_1", 0);
    psx.fieldAt(i32, &s1.unk5C, 0).* = psx.fieldAt(i32, &obj.unkC, 0).*;
    s1.unk5F = 0;
    s1.unk1C = psx.conv(i16, (@as(c_int, s1.unk5C) << 7) +% 0x40);
    s1.unk20 = psx.conv(i16, (@as(c_int, s1.unk5E) << 7) +% 0x40);
    s1.unk1E = psx.conv(i16, c.vs_field_sampleActorSupportHeight(@ptrCast(&s1.unk1C), 0, 0, 1) -% (@as(c_int, obj.variant) << 7));
    _ = c.vs_field_setGimmickOrientation(@as(c_int, obj.index), @as(c_int, obj.material));
    c.vs_battle_aiAddAiTileOccupant(@ptrCast(s1));
    psx.setBits(&s1.unk6C[8], "unk0_4", 0);

    if (@as(c_int, psx.getBits(&s1.unk6C[8], "actorId")) > 4) {
        var j: c_int = 0;
        while (j < @as(c_int, obj.index)) : (j += 1) {
            const temp_a0 = psx.at(t, j).* orelse continue;
            if (psx.fieldAt(i32, &temp_a0.unk5C, 0).* == psx.fieldAt(i32, &s1.unk5C, 0).* and
                @as(c_int, temp_a0.unk1E) == @as(c_int, s1.unk1E) +% 0x80 and
                @as(c_int, psx.getBits(&temp_a0.unk6C[8], "actorId")) > 4)
            {
                if (@as(c_int, psx.getBits(&temp_a0.unk6C[8], "actorId")) !=
                    @as(c_int, psx.getBits(&s1.unk6C[8], "actorId")))
                {
                    psx.setBits(&s1.unk6C[8], "unk0_4", 1);
                } else {
                    psx.setBits(&s1.unk6C[8], "unk0_4", 2);
                    s1.unk1E = psx.conv(i16, @as(c_int, s1.unk1E) -% 0x80);
                    c.vs_battle_aiAddAiTileOccupant(@ptrCast(s1));
                }
                break;
            }
        }
    }

    return 0;
}
