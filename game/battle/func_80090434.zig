//! The sparkle particles: they drift on the view and are drawn as one or two tiles each (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const gte = @import("common").gte;
const v = @import("views_146C");
const c = @import("c_146C");
const sprite_item = @import("common").sprite_item;

/// A corner (x low, y high); a colour as the grey byte the packet's three channels took.
fn xy(x: c_int, y: c_int) c_int {
    return (x & 0xFFFF) | (y << 16);
}
fn grey(color: c_int) c_int {
    return @as(c_int, psx.conv(u8, color)) * 0x010101;
}

/// orig: func_80090434 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// Moves each particle on the view and draws it as one or two tiles into the ordering table.
pub fn func_80090434() callconv(.c) void {
    const scratch = v.g.vs_scratch();

    const view = gte.viewOf(&v.g.vs_view().viewMatrix, gte.no_screen);

    var origin: v.VECTOR = undefined;
    var velocity: v.VECTOR = undefined;
    var flags: c.psx_long = undefined;
    gte.transformTo(&origin, &view, v.g.D_800F1CE0());
    gte.transformTo(&velocity, &view, v.g.D_800F1D98());

    // The slot 4 entries past the table's base.
    const ot: ?*anyopaque = @ptrFromInt(@intFromPtr(psx.get(scratch.unk4)) +% 4 * @sizeOf(c_uint));
    origin.vx = (velocity.vx -% origin.vx) >> 3;
    origin.vy = (velocity.vy -% origin.vy) >> 3;
    origin.vz = (velocity.vz -% origin.vz) >> 3;

    if (v.g.vs_field_areRoomUpdatesPaused().* == 0) {
        if (v.g.D_800F1DC7().* != 0) {
            v.g.D_800F1DC7().* -%= 8;
        }
    } else if (v.g.D_800F1DC7().* < 96) {
        v.g.D_800F1DC7().* +%= 8;
    }

    var particleAddr: usize = @intFromPtr(v.g.vs_field_weatherParticles().*);
    var particle: *allowzero align(1) v.D_800F1DD4_t = @ptrFromInt(particleAddr);

    flags = 0;
    while (flags < v.g.vs_field_weatherParticleCount().*) : ({
        flags +%= 1;
        particleAddr +%= @sizeOf(v.D_800F1DD4_t);
        particle = @ptrFromInt(particleAddr);
    }) {
        if (v.g.vs_field_areRoomUpdatesPaused().* == 0) {
            var j: c_int = 0;
            while (j < (v.g.vs_gametime_tickspeed().* >> 1)) : (j +%= 1) {
                particle.unkF = psx.conv(u8, @as(c_int, particle.unkF) +% ((c.rand() & 15) -% 7));
                particle.unkF &= 127;
                if (particle.unkD != 0) {
                    particle.unk0 = psx.conv(i16, @as(c_int, particle.unk0) +%
                        (origin.vx +% (c.rsin(@as(c_int, particle.unkF) << 5) >> 9)));
                    particle.unk4 = psx.conv(i16, @as(c_int, particle.unk4) +%
                        (origin.vz -% (c.rcos(@as(c_int, particle.unkF) << 5) >> 9)));
                    particle.unk2 = psx.conv(i16, @as(c_int, particle.unk2) -%
                        (c.rsin(@as(c_int, particle.unkD) << 6) >> 9));
                    particle.unkD +%= 1;
                    if (particle.unkD > 32) {
                        particle.unkD = 0;
                    }
                } else {
                    particle.unk0 = psx.conv(i16, @as(c_int, particle.unk0) +%
                        (origin.vx +% (c.rsin(@as(c_int, particle.unkF) << 5) >> 10)));
                    particle.unk4 = psx.conv(i16, @as(c_int, particle.unk4) +%
                        (origin.vz -% (c.rcos(@as(c_int, particle.unkF) << 5) >> 10)));
                    if ((c.rand() & 127) == 0) {
                        particle.unkD = 1;
                    }
                }
                particle.unk2 = psx.conv(i16, @as(c_int, particle.unk2) +% origin.vy);
                particle.unk4 &= 255;
                if (particle.unk0 < 0) {
                    particle.unk0 = psx.conv(i16, @as(c_int, particle.unk0) +% 320);
                }
                if (particle.unk0 > 320) {
                    particle.unk0 = psx.conv(i16, @as(c_int, particle.unk0) -% 320);
                }
                if (particle.unk2 < 0) {
                    particle.unk2 = psx.conv(i16, @as(c_int, particle.unk2) +% 240);
                }
                if (particle.unk2 > 240) {
                    particle.unk2 = psx.conv(i16, @as(c_int, particle.unk2) -% 240);
                }
            }
        }

        const x: c_int = @divTrunc((@as(c_int, particle.unk0) -% 160) << 12,
            (@as(c_int, particle.unk4) << 2) +% 3072) +% 160;
        const y: c_int = @divTrunc((@as(c_int, particle.unk2) -% 120) << 12,
            (@as(c_int, particle.unk4) << 2) +% 3072) +% 120;
        const brightness = @divTrunc(255 -% @as(c_int, particle.unk4), 4);
        const fade = @as(c_int, v.g.D_800F1DC7().*) -% 32;
        var color: c_int = brightness -% fade;
        if (color < 0) {
            color = 0;
        }

        const size: c_int = if ((flags & 1) != 0) 0x20002 else 0x10001;
        // Each sparkle is one semi-transparent tile, or three when it has a previous place (a trail halving in brightness).
        if (psx.conv(c_uint, x) < 320 and psx.conv(c_uint, y) < 240) {
            sprite_item.tileFlat(ot, true, grey(color), xy(x, y), size);
        }

        if (psx.conv(u16, particle.unk8) < 320 and psx.conv(u16, particle.unkA) < 240) {
            color >>= 1;
            sprite_item.tileFlat(ot, true, grey(color), xy(@as(c_int, particle.unk8), @as(c_int, particle.unkA)), size);
            color >>= 1;
            sprite_item.tileFlat(ot, true, grey(color), xy(x -% ((x -% @as(c_int, particle.unk8)) *% 2), y -% ((y -% @as(c_int, particle.unkA)) *% 2)), size);
        }

        if (v.g.vs_field_areRoomUpdatesPaused().* == 0) {
            particle.unk8 = psx.conv(i16, x);
            particle.unkA = psx.conv(i16, y);
        }
    }

    c.pc_insert_tpage(4, 0x31);
}
