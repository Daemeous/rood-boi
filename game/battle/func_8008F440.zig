//! The rain: falling drops as lines, landed drops as splashes (src/BATTLE/BATTLE.PRG/146C.c).
const psx = @import("common").psx;
const v = @import("views_146C");
const c = @import("c_146C");

/// `ot + depth` with the C's 4-byte u_long entries.
fn ordering(scratch: *v.vs_scratch_t, depth: c_int) ?*anyopaque {
    return @ptrCast(psx.at(@as([*]u32, @ptrCast(@alignCast(psx.get(scratch.unk4).?))), depth));
}

/// orig: func_8008F440 (src/BATTLE/BATTLE.PRG/146C.c)
/// For each of the D_800F1DC0 drops: a falling drop moves by the wind D_800F1D98 per tick (its fall scaled by its
/// unkE) and, when it projects on screen, is drawn as a semi-transparent line back along the wind; off screen (or
/// too near or far) it is recycled (func_80090A1C). A drop above the floor that was drawn on an odd slot lands
/// where its cell (func_8008B764) is not a hole, at the floor height (func_8008DC7C). A landed drop is drawn as a
/// growing splash quad (frame unkD / 2) and recycled after 8 frames or off screen.
pub fn func_8008F440() callconv(.c) void {
    var vertices: [10]v.SVECTOR = undefined;
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
                    // x86 shifts by the amount & 31 (unkE is 0 or 2 here).
                    drop.unk2 = psx.conv(i16, @as(c_int, drop.unk2) +% (@as(c_int, wind[1]) << @truncate(drop.unkE)));
                    if (drop.unkE == 0) {
                        drop.unk0 = psx.conv(i16, @as(c_int, drop.unk0) +% wind[0]);
                        drop.unk4 = psx.conv(i16, @as(c_int, drop.unk4) +% wind[2]);
                    }
                }
            }
            drop.unkE = 2;
            var line: [*]v.LINE_G2 = @ptrCast(@alignCast(psx.get(scratch.unk0).?));
            const depth = c.RotTransPers(@ptrCast(drop), @ptrCast(&line[0].x0), &projection, &flags);
            if (psx.conv(u16, @as(c_int, line[0].x0) +% 639) < 1599 and depth < 2048) {
                if (psx.conv(c_uint, @as(c_int, @as(u16, @bitCast(line[0].y0))) - 1) < 239 and line[0].x0 > 0 and
                    line[0].x0 < 320 and depth > 4)
                {
                    vertices[0].vx = psx.conv(i16, @as(c_int, drop.unk0) -% wind[0]);
                    vertices[0].vy = psx.conv(i16, @as(c_int, drop.unk2) -% wind[1]);
                    vertices[0].vz = psx.conv(i16, @as(c_int, drop.unk4) -% wind[2]);
                    _ = c.RotTransPers(@ptrCast(&vertices[0]), @ptrCast(&line[0].x1), &projection, &flags);
                    // setLineG2 and setSemiTrans(line, 1)
                    const tag: *v.P_TAG = @ptrCast(&line[0]);
                    psx.setBits(tag, "len", 4);
                    tag.code = 0x50;
                    tag.code |= 0x02;
                    line[0].r0 = 96;
                    line[0].g0 = 96;
                    line[0].b0 = 128;
                    line[0].r1 = 32;
                    line[0].g1 = 32;
                    line[0].b1 = 100;
                    c.pc_weather_drop_packet(&line[0], drop.unk0, drop.unk2, drop.unk4);
                    c.AddPrim(ordering(scratch, depth), &line[0]);
                    line += 1;
                    psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(&line[0])));
                    c.pc_insert_tpage(depth, 96);
                    drop.unkE = 0;
                }
            } else {
                c.func_80090A1C(@ptrCast(drop));
            }
            if (drop.unk2 > 0) {
                if (drop.unkE == 0 and (i & 1) != 0) {
                    const cell = c.func_8008B764(psx.conv(c_uint, @divTrunc(@as(c_int, drop.unk0), 128)), psx.conv(c_uint, @divTrunc(@as(c_int, drop.unk4), 128)), 0);
                    if (cell != null and (@as(*u32, @ptrCast(@alignCast(cell))).* & 31) != 31) {
                        drop.unkC = 1;
                        projection = (@as(c_int, c.func_8008DC7C(drop.unk0, drop.unk4)) << 17) >> 17;
                        drop.unk2 = psx.conv(i16, projection);
                    } else {
                        c.func_80090A1C(@ptrCast(drop));
                    }
                } else {
                    c.func_80090A1C(@ptrCast(drop));
                }
            }
        } else {
            const corners = [4][2]c_int{ .{ -24, -24 }, .{ 24, -24 }, .{ -24, 24 }, .{ 24, 24 } };
            for (corners, 0..) |corner, k| {
                vertices[k].vx = psx.conv(i16, @as(c_int, drop.unk0) +% corner[0]);
                vertices[k].vy = drop.unk2;
                vertices[k].vz = psx.conv(i16, @as(c_int, drop.unk4) +% corner[1]);
            }
            var splash: [*]v.POLY_FT4 = @ptrCast(@alignCast(psx.get(scratch.unk0).?));
            var depth = c.RotTransPers4(@ptrCast(&vertices[0]), @ptrCast(&vertices[1]), @ptrCast(&vertices[2]), @ptrCast(&vertices[3]),
                @ptrCast(&splash[0].x0), @ptrCast(&splash[0].x1), @ptrCast(&splash[0].x2), @ptrCast(&splash[0].x3), &projection, &flags);
            depth -%= 64;
            if (psx.conv(c_uint, depth -% 1) < 2047) {
                // setPolyFT4 and setSemiTrans(splash, 1)
                const tag: *v.P_TAG = @ptrCast(&splash[0]);
                psx.setBits(tag, "len", 9);
                tag.code = 0x2C;
                tag.code |= 0x02;
                const u = psx.conv(u8, @as(c_int, drop.unkD >> 1) * 16); // a byte times 16: no overflow
                splash[0].u0 = u;
                splash[0].v0 = 192;
                splash[0].u1 = psx.conv(u8, @as(c_int, drop.unkD >> 1) * 16 + 16); // no overflow
                splash[0].v1 = 192;
                splash[0].u2 = u;
                splash[0].v2 = 208;
                splash[0].u3 = psx.conv(u8, @as(c_int, drop.unkD >> 1) * 16 + 16); // no overflow
                splash[0].v3 = 208;
                splash[0].r0 = 128;
                splash[0].g0 = 128;
                splash[0].b0 = 128;
                splash[0].tpage = 0x71;
                splash[0].clut = 0x38BF;
                c.pc_weather_drop_packet(&splash[0], drop.unk0, drop.unk2, drop.unk4);
                c.AddPrim(ordering(scratch, depth), &splash[0]);
                splash += 1;
                psx.put(&scratch.unk0, @as(*anyopaque, @ptrCast(&splash[0])));
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
