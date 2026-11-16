//! Draws an actor's emote (the speech-bubble icons over its head) (pc/src/game/BATTLE/BATTLE.PRG/2842C.c).
const common = @import("common");
const gte = common.gte;
const psx = common.psx;
const sprite_item = @import("common").sprite_item;
const v = @import("views_2842C");
const c = @import("c_2842C");


/// The scratchpad's u_long* slot as a guest pointer.
inline fn slot(field: anytype) [*c]c.u_long {
    return @ptrCast(@alignCast(psx.get(field.*)));
}

/// *(int*)&position: vx in the low half, vy in the high half.
inline fn packed_xy(p: *const v.SVECTOR) c_int {
    return @as(*align(1) const c_int, @ptrCast(p)).*;
}

/// 1 << actor (x86 shifts by actor & 31).
inline fn bit(actor: c_int) c_int {
    return @as(c_int, 1) << @truncate(@as(c_uint, @bitCast(actor)));
}

/// orig: func_80095C18 (pc/src/game/BATTLE/BATTLE.PRG/2842C.c)
/// Draws emote `kind` for `actor`: without bit 3, an icon beside its screen point (sprites, or the bubble
/// polygon; kind 4 off screen becomes the edge arrow), facing the side the player stands on; with bit 3, the
/// animated thought bubble over a distant actor. Each decision is recorded in pc_emote_record (PC_CLASSIC).
pub fn func_80095C18(actor: c_int, kind_arg: c_int) callconv(.c) void {
    var kind = kind_arg;
    var position: v.SVECTOR align(4) = undefined; // RotTransPers writes its sxy word here
    var other: v.SVECTOR = undefined;
    var depth: c_int = undefined;
    var side: c_int = undefined;
    const flagsWord = v.g.D_800F2274();
    const record = v.g.pc_emote_record();

    if (kind & 8 == 0) {
        if (kind & 0x80 != 0) {
            kind &= 7;
            if (psx.conv(c_uint, kind -% 1) >= 4) kind = 5;
        }
        if (c.vs_actors_targetPoint(actor, 251, psx.cptr(c.SVECTOR, &position), 0) != 0) return;
        if (c.vs_actors_targetPoint(0, 251, psx.cptr(c.SVECTOR, &other), 0) != 0) return;
        var z: c_int = @as(c_int, position.vz) -% 16;
        const xy = packed_xy(&position);
        if (@as(c_int, other.vx) +% 32 < position.vx) flagsWord.* &= ~bit(actor);
        if (position.vx < @as(c_int, other.vx) -% 32) flagsWord.* |= bit(actor);
        if (z < 64) {
            z = 64;
        } else if (z >= 2048) {
            z = 2047;
        }
        const ot = slot(&common.scratch.get().unk4) + @as(usize, @intCast(z)); // 64 <= z < 2048
        side = (flagsWord.* >> @truncate(@as(c_uint, @bitCast(actor)))) & 1;

        record.draw = 1;
        record.kind = kind;
        record.side = side;
        record.x = position.vx;
        record.y = position.vy;
        record.z = z;

        const offsets = v.g.D_80068F94();
        switch (kind) {
            0 => return,
            1, 2, 3 => {
                while (kind != 0) {
                    kind -%= 1;
                    _ = sprite_item.sprite(ot, 128, xy +% kind *% 6 +% psx.at(offsets, side).*, 0x50005, 0, 0x373E125A);
                }
            },
            4 => {
                var flags: c_int = 0;
                if (position.vx < 16) {
                    position.vx = 16;
                    flags = 1;
                }
                if (position.vx > 304) {
                    position.vx = 304;
                    flags |= 2;
                }
                if (flags != 0) {
                    if (position.vy < 40) {
                        position.vy = 40;
                        flags |= 4;
                    }
                } else if (position.vy < 8) {
                    position.vy = 40;
                    flags = 4;
                }
                if (position.vy > 208) {
                    position.vy = 208;
                    flags |= 8;
                }
                if (flags != 0) {
                    record.draw = 2;
                    record.direction = flags;
                    record.x = position.vx;
                    record.y = position.vy;
                    c.func_8009651C(psx.cptr(c.SVECTOR, &position), flags);
                    return;
                }
                side +%= 2;
                record.side = side;
                _ = sprite_item.sprite(ot, 128, xy +% psx.at(offsets, side).*, 0xC0007, 0, 0x373E1198);
            },
            5 => {
                _ = sprite_item.sprite(ot, 128, @bitCast(@as(c_uint, @bitCast(xy +% psx.at(offsets, side).*)) +% 0xFFFD0002), 0x100010, 0, 0x373E2098);
            },
            6 => {
                side +%= 4;
                record.side = side;
            },
            7 => {
                _ = sprite_item.sprite(ot, 128, @bitCast(@as(c_uint, @bitCast(xy)) +% 0xFFEA0000), 0xC0007, @bitCast(@as(c_uint, 0xE1000017)), 0x373E11B7);
                return;
            },
            else => {},
        }
        const corners = psx.at(v.g.D_80068F04(), side);
        const uv = psx.at(v.g.D_80068F64(), side);
        // The bubble's quad (colour 0x808080, page 0x17, CLUT 0x373E), placed at the head of the slot as the packet was.
        _ = sprite_item.ft4(ot, false, false, 0x17, 0x373E, 0x808080,
            &[4]c_int{ c.vs_core_mathAddPackedScreenCoordinates(xy, corners[0]), c.vs_core_mathAddPackedScreenCoordinates(xy, corners[1]), c.vs_core_mathAddPackedScreenCoordinates(xy, corners[2]), c.vs_core_mathAddPackedScreenCoordinates(xy, corners[3]) },
            &[4]c_int{ uv[0], uv[1], uv[2], uv[3] });
    } else {
        const bubbles = v.g.D_800F2260();
        if (c.vs_actors_anchorPoint(actor, 255, psx.cptr(c.SVECTOR, &other), 0) == 0) {
            if (c.vs_actors_anchorPoint(0, 255, psx.cptr(c.SVECTOR, &position), 0) != 0) return;
            position.vx = psx.conv(i16, @as(c_int, position.vx) -% other.vx);
            position.vz = psx.conv(i16, @as(c_int, position.vz) -% other.vz);
            side = @as(c_int, @abs(position.vx)) + @as(c_int, @abs(position.vz)); // ABS of two shorts: no overflow
            if (side < 352) flagsWord.* &= ~bit(actor);
            if (side > 416) flagsWord.* |= bit(actor);
            side = (flagsWord.* >> @truncate(@as(c_uint, @bitCast(actor)))) & 1;
            if (side != 0) return;
            const gteView = gte.viewOf(&v.g.vs_view().viewMatrix, gte.screenCurrent());
            other.vy = psx.conv(i16, @as(c_int, other.vy) -% 64);
            // PC_GTE_UNNARROWED
            v.g.pc_gte_anchored().* +%= 1;
            const projected = gte.project(&gteView, &other);
            @as(*align(1) c_int, @ptrCast(&position)).* = projected.sxy;
            depth = projected.ir0;
            side = @bitCast(projected.flag);
            v.g.pc_gte_anchored().* -%= 1;
            const ot = slot(&common.scratch.get().unk4);
            position.vx = psx.conv(i16, @as(c_int, position.vx) -% 8);
            position.vy = psx.conv(i16, @as(c_int, position.vy) -% 8);
            const xy = packed_xy(&position);
            depth = c.vs_field_gimmickSubtype(actor);
            if (depth >= 0) {
                if (depth == 0) {
                    const count = psx.at(bubbles, actor);
                    count.* +%= 1;
                    if (count.* & 7 == 0) count.* = 0;
                }
                record.draw = 3;
                record.kind = kind;
                record.frame = depth;
                record.x = position.vx;
                record.y = position.vy;
                _ = sprite_item.sprite(ot, 128, xy, 0x100010, sprite_item.page, ((depth << 4) +% 96) | 0x37FD2000);
                return;
            }
        }
        psx.at(bubbles, actor).* = 0;
    }
}
