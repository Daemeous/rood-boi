//! The snow: drifting flakes drawn as one or two tiles, landed flakes fading (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// `ot + depth` with the C's 4-byte u_long entries.
fn ordering(scratch: *v.vs_scratch_t, depth: c_int) ?*anyopaque {
    return @ptrCast(psx.at(@as([*]u32, @ptrCast(@alignCast(psx.get(scratch.unk4).?))), depth));
}

/// The widescreen margin (PC_GTE_UNNARROWED's 54 pixels each side).
fn margin() c_int {
    return if (v.g.pc_gte_widescreen().* != 0) 54 else 0;
}

/// setTile and setSemiTrans(prim, 1).
fn tile(prim: *v.TILE) void {
    const tag: *v.P_TAG = @ptrCast(prim);
    psx.setBits(tag, "len", 3);
    tag.code = 0x60;
    tag.code |= 0x02;
}

/// Projects a flake into the packet at the cursor (anchored for the widescreen hook).
fn project(drop: *v.D_800F1DD4_t, prim: *v.TILE, projection: *c_int, flags: *c_int) c_int {
    v.g.pc_gte_anchored().* +%= 1;
    const depth = c.RotTransPers(@ptrCast(drop), @ptrCast(&prim.x0), projection, flags);
    v.g.pc_gte_anchored().* -%= 1;
    return depth;
}

/// orig: func_8008FDC4 (src/BATTLE/BATTLE.PRG/146C.c)
/// For each of the D_800F1DC0 flakes: a falling flake (scaled by unkE) drops with the wind, an unscaled one
/// drifts in a random walk (unkF) and sometimes flutters up (unkD, 32 ticks); on screen it is drawn as a white
/// tile and a second one offset by its slot's angle (sizes 2 and 1 alternating), off screen recycled
/// (func_80090A1C). A drawn flake above the floor on an odd slot lands at the floor height (func_8008DC7C). A landed
/// flake fades out over 8 frames as a grey tile.
pub fn func_8008FDC4() callconv(.c) void {
    var projection: c_int = undefined;
    var flags: c_int = undefined;
    const scratch: *v.vs_scratch_t = @ptrFromInt(0x1F800000);
    c.SetRotMatrix(@ptrCast(&scratch.viewMatrix));
    c.SetTransMatrix(@ptrCast(&scratch.viewMatrix));
    const wind = v.g.D_800F1D98();
    var particle: [*]v.D_800F1DD4_t = @ptrCast(v.g.D_800F1DD4().*.?);
    var i: c_int = 0;
    while (i < v.g.D_800F1DC0().*) : ({
        i += 1; // i < a short
        particle += 1;
    }) {
        const drop = &particle[0];
        if (drop.unkC == 0) {
            if (v.g.D_800F1BA4().* == 0) {
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
            var prim: [*]v.TILE = @ptrCast(@alignCast(psx.get(scratch.unk0).?));
            const depth = project(drop, &prim[0], &projection, &flags);
            if (psx.conv(u16, @as(c_int, prim[0].x0) +% 639) < 1599 and depth < 2048) {
                if (depth > 4) {
                    const x = prim[0].x0;
                    const y = prim[0].y0;
                    if (psx.conv(c_uint, @as(c_int, @as(u16, @bitCast(y))) - 1) < 239 and x > -margin() and x < 320 + margin()) {
                        tile(&prim[0]);
                        if (i & 1 != 0) {
                            prim[0].w = 2;
                            prim[0].h = 2;
                        } else {
                            prim[0].w = 1;
                            prim[0].h = 1;
                        }
                        prim[0].r0 = 255;
                        prim[0].g0 = 255;
                        prim[0].b0 = 255;
                        c.AddPrim(ordering(scratch, depth), &prim[0]);
                        prim += 1;
                        drop.unkE = 0;
                    }
                    projection = @as(c_int, x) +% (c.rsin(i << 5) >> 6);
                    flags = @as(c_int, y) -% (c.rcos(i << 5) >> 6);
                    if (psx.conv(c_uint, flags -% 1) < 239 and projection > -margin() and projection < 320 + margin() and depth > 4) {
                        tile(&prim[0]);
                        prim[0].x0 = psx.conv(i16, projection);
                        prim[0].y0 = psx.conv(i16, flags);
                        if ((i & 1) == 0) {
                            prim[0].w = 2;
                            prim[0].h = 2;
                        } else {
                            prim[0].w = 1;
                            prim[0].h = 1;
                        }
                        prim[0].r0 = 255;
                        prim[0].g0 = 255;
                        prim[0].b0 = 255;
                        c.AddPrim(ordering(scratch, depth), &prim[0]);
                        prim += 1;
                        drop.unkE = 0;
                    }
                    if (drop.unkE == 0) {
                        psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(&prim[0])));
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
                    projection = (@as(c_int, c.func_8008DC7C(drop.unk0, drop.unk4)) << 17) >> 17;
                    drop.unk2 = psx.conv(i16, projection);
                } else {
                    c.func_80090A1C(@ptrCast(drop));
                }
            }
        } else {
            var prim: [*]v.TILE = @ptrCast(@alignCast(psx.get(scratch.unk0).?));
            const depth = project(drop, &prim[0], &projection, &flags);
            if (psx.conv(c_uint, @as(c_int, @as(u16, @bitCast(prim[0].y0))) - 1) < 239 and prim[0].x0 > -margin() and
                prim[0].x0 < 320 + margin() and depth > 4)
            {
                tile(&prim[0]);
                prim[0].w = 2;
                prim[0].h = 2;
                const grey = psx.conv(u8, 128 -% (@as(c_int, drop.unkD) << 4));
                prim[0].r0 = grey;
                prim[0].g0 = grey;
                prim[0].b0 = grey;
                c.AddPrim(ordering(scratch, depth), &prim[0]);
                prim += 1;
                psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(&prim[0])));
                c.pc_insert_tpage(depth, 96);
                if (v.g.D_800F1BA4().* == 0) {
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
