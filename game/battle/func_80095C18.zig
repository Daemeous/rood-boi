//! Draws an actor's emote (the speech-bubble icons over its head) (src/BATTLE/BATTLE.PRG/2842C.c).
const psx = @import("common").psx;
const v = @import("views_2842C");
const c = @import("c_2842C");

const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);

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

/// orig: func_80095C18 (src/BATTLE/BATTLE.PRG/2842C.c)
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
        if (c.func_800A1280(actor, 251, psx.cptr(c.SVECTOR, &position), 0) != 0) return;
        if (c.func_800A1280(0, 251, psx.cptr(c.SVECTOR, &other), 0) != 0) return;
        var z: c_int = @as(c_int, position.vz) -% 16;
        const xy = packed_xy(&position);
        if (@as(c_int, other.vx) +% 32 < position.vx) flagsWord.* &= ~bit(actor);
        if (position.vx < @as(c_int, other.vx) -% 32) flagsWord.* |= bit(actor);
        if (z < 64) {
            z = 64;
        } else if (z >= 2048) {
            z = 2047;
        }
        const ot = slot(&scratch.unk4) + @as(usize, @intCast(z)); // 64 <= z < 2048
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
                    c.vs_battle_setSprite(128, xy +% kind *% 6 +% psx.at(offsets, side).*, 0x50005, ot)[4] = 0x373E125A;
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
                c.vs_battle_setSprite(128, xy +% psx.at(offsets, side).*, 0xC0007, ot)[4] = 0x373E1198;
            },
            5 => {
                c.vs_battle_setSprite(128, @bitCast(@as(c_uint, @bitCast(xy +% psx.at(offsets, side).*)) +% 0xFFFD0002), 0x100010, ot)[4] = 0x373E2098;
            },
            6 => {
                side +%= 4;
                record.side = side;
            },
            7 => {
                const prim = c.vs_battle_setSprite(128, @bitCast(@as(c_uint, @bitCast(xy)) +% 0xFFEA0000), 0xC0007, ot);
                prim[1] = 0xE1000017;
                prim[4] = 0x373E11B7;
                return;
            },
            else => {},
        }
        const prim = slot(&scratch.unk0);
        const corners = psx.at(v.g.D_80068F04(), side);
        const uv = psx.at(v.g.D_80068F64(), side);
        prim[0] = (ot[0] & 0xFFFFFF) | 0x9000000;
        prim[1] = 0x2C808080;
        prim[2] = @bitCast(c.func_80096254(xy, corners[0]));
        prim[3] = @as(c_uint, uv[0]) | 0x373E0000;
        prim[4] = @bitCast(c.func_80096254(xy, corners[1]));
        prim[5] = @as(c_uint, uv[1]) | 0x170000;
        prim[6] = @bitCast(c.func_80096254(xy, corners[2]));
        prim[7] = uv[2];
        prim[8] = @bitCast(c.func_80096254(xy, corners[3]));
        prim[9] = uv[3];
        ot[0] = (psx.addr(prim) << 8) >> 8;
        psx.put(&scratch.unk0, @as(?*anyopaque, @ptrCast(prim + 10)));
    } else {
        const bubbles = v.g.D_800F2260();
        if (c.func_800A190C(actor, 255, psx.cptr(c.SVECTOR, &other), 0) == 0) {
            if (c.func_800A190C(0, 255, psx.cptr(c.SVECTOR, &position), 0) != 0) return;
            position.vx = psx.conv(i16, @as(c_int, position.vx) -% other.vx);
            position.vz = psx.conv(i16, @as(c_int, position.vz) -% other.vz);
            side = @as(c_int, @abs(position.vx)) + @as(c_int, @abs(position.vz)); // ABS of two shorts: no overflow
            if (side < 352) flagsWord.* &= ~bit(actor);
            if (side > 416) flagsWord.* |= bit(actor);
            side = (flagsWord.* >> @truncate(@as(c_uint, @bitCast(actor)))) & 1;
            if (side != 0) return;
            c.SetRotMatrix(psx.cptr(c.MATRIX, &scratch.viewMatrix));
            c.SetTransMatrix(psx.cptr(c.MATRIX, &scratch.viewMatrix));
            other.vy = psx.conv(i16, @as(c_int, other.vy) -% 64);
            // PC_GTE_UNNARROWED
            v.g.pc_gte_anchored().* +%= 1;
            _ = c.RotTransPers(psx.cptr(c.SVECTOR, &other), @ptrCast(&position), &depth, &side);
            v.g.pc_gte_anchored().* -%= 1;
            const ot = slot(&scratch.unk4);
            position.vx = psx.conv(i16, @as(c_int, position.vx) -% 8);
            position.vy = psx.conv(i16, @as(c_int, position.vy) -% 8);
            const xy = packed_xy(&position);
            depth = c.func_8009F858(actor);
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
                const prim = c.vs_battle_setSprite(128, xy, 0x100010, ot);
                prim[1] = 0xE100000C;
                prim[4] = @bitCast(((depth << 4) +% 96) | 0x37FD2000);
                return;
            }
        }
        psx.at(bubbles, actor).* = 0;
    }
}
