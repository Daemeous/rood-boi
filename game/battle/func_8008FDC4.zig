//! The snow: drifting flakes drawn as one or two tiles, landed flakes fading (pc/src/game/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const gte = @import("common").gte;
const v = @import("views_146C");
const c = @import("c_146C");
const sprite_item = @import("common").sprite_item;

/// `ot + depth` with the C's 4-byte u_long entries.
fn ordering(scratch: *v.vs_scratch_t, depth: c_int) ?*anyopaque {
    return @ptrCast(psx.at(@as([*]u32, @ptrCast(@alignCast(psx.get(scratch.unk4).?))), depth));
}

/// The widescreen margin (PC_GTE_UNNARROWED's 54 pixels each side).
fn margin() c_int {
    return if (v.g.pc_gte_widescreen().* != 0) 54 else 0;
}

/// A corner as x low, y high.
fn xy(x: c_int, y: c_int) c_int {
    return (x & 0xFFFF) | (y << 16);
}

/// A semi-transparent tile of the grey `grey` (0xFF: white), `size` pixels square, at the slot `ot`.
fn flake(ot: ?*anyopaque, x: c_int, y: c_int, size: c_int, grey: c_int) void {
    sprite_item.tileFlat(ot, true, (grey & 0xFF) * 0x010101, xy(x, y), size | (size << 16));
}

/// Projects a flake (anchored for the widescreen hook); its screen place as x low, y high in `sxy`.
fn project(view: *const gte.View, drop: *v.D_800F1DD4_t, sxy: *c_int, projection: *c_int, flags: *c_int) c_int {
    v.g.pc_gte_anchored().* +%= 1;
    const projected = gte.project(view, drop);
    v.g.pc_gte_anchored().* -%= 1;
    sxy.* = projected.sxy;
    projection.* = projected.ir0;
    flags.* = @bitCast(projected.flag);
    return @intCast(projected.otz);
}

/// orig: func_8008FDC4 (pc/src/game/BATTLE/BATTLE.PRG/146C.c)
/// For each of the vs_field_weatherParticleCount flakes: a falling flake (scaled by unkE) drops with the wind, an unscaled one
/// drifts in a random walk (unkF) and sometimes flutters up (unkD, 32 ticks); on screen it is drawn as a white
/// tile and a second one offset by its slot's angle (sizes 2 and 1 alternating), off screen recycled
/// (func_80090A1C). A drawn flake above the floor on an odd slot lands at the floor height (vs_field_floorHeightAt). A landed
/// flake fades out over 8 frames as a grey tile.
pub fn func_8008FDC4() callconv(.c) void {
    var projection: c_int = undefined;
    var flags: c_int = undefined;
    const scratch = v.g.vs_scratch();
    const view = gte.viewOf(&v.g.vs_view().viewMatrix, gte.screenCurrent());
    const wind = v.g.D_800F1D98();
    var particle: [*]v.D_800F1DD4_t = @ptrCast(v.g.vs_field_weatherParticles().*.?);
    var i: c_int = 0;
    while (i < v.g.vs_field_weatherParticleCount().*) : ({
        i += 1; // i < a short
        particle += 1;
    }) {
        const drop = &particle[0];
        if (drop.unkC == 0) {
            if (v.g.vs_field_areRoomUpdatesPaused().* == 0) {
                var j: c_int = 0;
                while (j < (v.g.vs_gametime_tickspeed().* >> 1)) : (j += 1) { // j < an int
                    if (drop.unkE != 0) {
                        // x86 shifts by the amount & 31 (unkE is 2 here).
                        drop.unk2 = psx.conv(i16, @as(c_int, drop.unk2) +% (@as(c_int, wind[1]) << @truncate(drop.unkE)));
                    } else {
                        drop.unkF = psx.conv(u8, @as(c_int, drop.unkF) +% ((c.rand() & 15) - 7));
                        drop.unkF &= 127;
                        if (drop.unkD != 0) {
                            drop.unk0 = psx.conv(i16, @as(c_int, drop.unk0) +% ((wind[0] >> 3) +% (c.rsin(@as(c_int, drop.unkF) << 5) >> 9)));
                            drop.unk4 = psx.conv(i16, @as(c_int, drop.unk4) +% ((wind[2] >> 3) -% (c.rcos(@as(c_int, drop.unkF) << 5) >> 9)));
                            drop.unk2 = psx.conv(i16, @as(c_int, drop.unk2) -% (c.rsin(@as(c_int, drop.unkD) << 6) >> 9));
                            drop.unkD +%= 1;
                            if (drop.unkD > 32) {
                                drop.unkD = 0;
                            }
                        } else {
                            drop.unk0 = psx.conv(i16, @as(c_int, drop.unk0) +% ((wind[0] >> 3) +% (c.rsin(@as(c_int, drop.unkF) << 5) >> 10)));
                            drop.unk4 = psx.conv(i16, @as(c_int, drop.unk4) +% ((wind[2] >> 3) -% (c.rcos(@as(c_int, drop.unkF) << 5) >> 10)));
                            if ((c.rand() & 127) == 0) {
                                drop.unkD = 1;
                            }
                        }
                        drop.unk2 = psx.conv(i16, @as(c_int, drop.unk2) +% (wind[1] >> 3));
                    }
                }
            }
            drop.unkE = 2;
            var sxy: c_int = 0;
            const depth = project(&view, drop, &sxy, &projection, &flags);
            if (psx.conv(u16, @as(c_int, psx.conv(i16, sxy)) +% 639) < 1599 and depth < 2048) {
                if (depth > 4) {
                    const x: c_int = psx.conv(i16, sxy);
                    const y: c_int = psx.conv(i16, sxy >> 16);
                    if (psx.conv(c_uint, @as(c_int, psx.conv(u16, y)) - 1) < 239 and x > -margin() and x < 320 + margin()) {
                        flake(ordering(scratch, depth), x, y, if (i & 1 != 0) 2 else 1, 255);
                        drop.unkE = 0;
                    }
                    projection = x +% (c.rsin(i << 5) >> 6);
                    flags = y -% (c.rcos(i << 5) >> 6);
                    if (psx.conv(c_uint, flags -% 1) < 239 and projection > -margin() and projection < 320 + margin() and depth > 4) {
                        flake(ordering(scratch, depth), projection, flags, if ((i & 1) == 0) 2 else 1, 255);
                        drop.unkE = 0;
                    }
                    if (drop.unkE == 0) {
                        c.pc_insert_tpage(depth, 0x71);
                    }
                }
            } else {
                c.func_80090A1C(@ptrCast(drop));
            }
            if (drop.unk2 > 0) {
                if (drop.unkE == 0 and (i & 1) != 0) {
                    drop.unkC = 1;
                    drop.unkD = 0;
                    projection = (@as(c_int, c.vs_field_floorHeightAt(drop.unk0, drop.unk4)) << 17) >> 17;
                    drop.unk2 = psx.conv(i16, projection);
                } else {
                    c.func_80090A1C(@ptrCast(drop));
                }
            }
        } else {
            var sxy: c_int = 0;
            const depth = project(&view, drop, &sxy, &projection, &flags);
            if (psx.conv(c_uint, @as(c_int, psx.conv(u16, sxy >> 16)) - 1) < 239 and psx.conv(i16, sxy) > -margin() and
                psx.conv(i16, sxy) < 320 + margin() and depth > 4)
            {
                flake(ordering(scratch, depth), psx.conv(i16, sxy), psx.conv(i16, sxy >> 16), 2, psx.conv(u8, 128 -% (@as(c_int, drop.unkD) << 4)));
                c.pc_insert_tpage(depth, 96);
                if (v.g.vs_field_areRoomUpdatesPaused().* == 0) {
                    drop.unkD = psx.conv(u8, @as(c_int, drop.unkD) +% (v.g.vs_gametime_tickspeed().* >> 1));
                }
                if (drop.unkD >= 8) {
                    c.func_80090A1C(@ptrCast(drop));
                }
            } else {
                c.func_80090A1C(@ptrCast(drop));
            }
        }
    }
}
