//! Area Map screen renderer (D2: 5A624.s; oracle pc/src/map_view.c).
const c = @import("c_map_view");
const v = @import("views_map_view");
const psx = @import("common").psx;
const gte = @import("common").gte;

const SpanShape = *const fn (?*const anyopaque, c_int, f32, f32, f32, f32) callconv(.c) void;
const AnchorPackets = *const fn (?*const anyopaque, ?*const anyopaque, c_int) callconv(.c) void;

const scratch_base: usize = 0x1f800000;

/// orig: pc_map_partner (pc/src/map_view.c)
/// The emitted span of row `row` overlapping [x0, x1] the most, or -1.
fn mapPartner(row: c_int, x0: c_int, x1: c_int) c_int {
    var best: c_int = -1;
    var most: c_int = 0;
    const emitted = v.g.pc_map_emitted();
    var i: c_int = 0;
    while (i < v.g.pc_map_emitted_count().*) : (i +%= 1) {
        const entry = &emitted[@intCast(i)]; // the count never exceeds PC_MAP_EMITTED
        if (@as(c_int, entry.row) != row) continue;
        const lo = if (x0 > @as(c_int, entry.x0)) x0 else @as(c_int, entry.x0);
        const hi = if (x1 < @as(c_int, entry.x1)) x1 else @as(c_int, entry.x1);
        if (hi -% lo +% 1 > most) {
            most = hi -% lo +% 1;
            best = i;
        }
    }
    return best;
}

/// orig: pc_map_span_shapes (pc/src/map_view.c)
/// Joins each emitted row to the rows above and below for the hardware renderer.
fn mapSpanShapes() void {
    const shape = psx.weak(SpanShape, "pc_hw_span_shape") orelse return;
    const emitted = v.g.pc_map_emitted();
    var i: c_int = 0;
    while (i < v.g.pc_map_emitted_count().*) : (i +%= 1) {
        const entry = &emitted[@intCast(i)]; // the count never exceeds PC_MAP_EMITTED
        const x0: c_int = entry.x0;
        const x1: c_int = entry.x1;
        const row: c_int = entry.row;
        var edge: [2][2]f32 = .{ .{ 0, 0 }, .{ 0, 0 } };
        var side: usize = 0;
        while (side < 2) : (side += 1) {
            const j = mapPartner(row +% (if (side != 0) @as(c_int, 1) else -1), x0, x1);
            if (j < 0) continue;
            const partner = &emitted[@intCast(j)]; // j < the count
            if (mapPartner(row, partner.x0, partner.x1) != i) continue;
            edge[side][0] = @as(f32, @floatFromInt(@as(c_int, partner.x0) -% x0)) * 0.5;
            edge[side][1] = @as(f32, @floatFromInt(@as(c_int, partner.x1) -% x1)) * 0.5;
        }
        shape(entry.packet, x1 -% x0, edge[0][0], edge[0][1], edge[1][0], edge[1][1]);
    }
}

/// orig: pc_map_recip (pc/src/map_view.c)
/// 0x8000 / n, read past the table's end as the original does.
fn mapRecip(dy: c_int) c_int {
    return psx.byteAt(u16, v.g.D_800EA130(), @as(isize, dy *% 2)).*;
}

/// orig: pc_map_step (pc/src/map_view.c)
/// Fixed-point x step per scanline.
fn mapStep(delta: c_int, dy: c_int) c_int {
    return (delta >> 15) *% mapRecip(dy);
}

/// orig: pc_map_rtps (pc/src/map_view.c)
fn mapRtps(xy: c_uint, z: c_int) c_uint {
    c.MTC2(xy, 0);
    c.MTC2(@bitCast(z), 1);
    gte.rtps();
    return c.MFC2(14);
}

/// orig: pc_map_row (pc/src/map_view.c)
/// A triangle/circle row buffer after the edge flags (the original never checks the scratchpad's end).
fn mapRow(offset: c_int) [*]i16 {
    if (offset < 0 or 0x1bc +% offset +% 2 > 0x400) c.pc_unimplemented("map row buffer beyond scratchpad");
    return @ptrFromInt(scratch_base +% @as(usize, @bitCast(@as(isize, 0x1bc +% offset))));
}

/// orig: pc_map_cell (pc/src/map_view.c)
/// One 9x9 cell's height from height `y`.
fn mapCell(out: *u8, cell: c_uint, y: c_int) void {
    const low: c_int = @as(c_int, @bitCast(cell << 15)) >> 17;
    const high: c_int = @as(c_int, @bitCast(cell)) >> 17;
    if (y < low) {
        if (high == 0 or high < y) out.* = 0;
    } else if (cell & 1 != 0) {
        var h = y -% low;
        if (h >= 0x31) h = 0x30;
        out.* = psx.conv(u8, h);
    }
}

fn rowAt(row: [*]i16, k: c_int) *i16 {
    return psx.at(row, k);
}

/// orig: pc_map_cut (pc/src/map_view.c)
/// Removes [left, right] from the first span of a row it touches.
fn mapCut(count: *u8, row: [*]i16, left: c_int, right: c_int) void {
    var i: c_int = 0;
    var j: c_int = undefined;
    if (count.* == 0) return;
    while (true) {
        const x1: c_int = rowAt(row, i +% 1).*;
        if (left < x1) {
            const x0: c_int = rowAt(row, i).*;
            if (!(x0 < right)) return;
            switch (@intFromBool(x0 < left) +% (if (right < x1) @as(c_int, 2) else 0)) {
                0 => {
                    count.* -%= 2;
                    j = i;
                    while (j < count.*) : (j +%= 1) rowAt(row, j).* = rowAt(row, j +% 2).*;
                    return;
                },
                1 => {
                    rowAt(row, i +% 1).* = psx.conv(i16, left);
                    return;
                },
                2 => {
                    rowAt(row, i).* = psx.conv(i16, right);
                    return;
                },
                else => {
                    j = @as(c_int, count.*) -% 1;
                    while (i < j) : (j -%= 1) rowAt(row, j +% 2).* = rowAt(row, j).*;
                    count.* +%= 2;
                    rowAt(row, j +% 1).* = psx.conv(i16, left);
                    rowAt(row, j +% 2).* = psx.conv(i16, right);
                    return;
                },
            }
        }
        i +%= 2;
        if (!(i < count.*)) return;
    }
}

/// orig: pc_map_cut_wall (pc/src/map_view.c)
/// Wall cuts against a lowered edge at x = p with wall limit `lim` (modes 0..2).
fn mapCutWall(count: *u8, row: [*]i16, p: c_int, lim: c_int, mode: c_int) void {
    var i: c_int = 0;
    var j: c_int = undefined;
    if (count.* == 0) return;
    while (true) {
        const x1: c_int = rowAt(row, i +% 1).*;
        if (p < x1) {
            const x0: c_int = rowAt(row, i).*;
            if (if (mode == 2) p < x0 else !(x0 < p)) return;
            if (mode == 0) {
                if (!(x0 < lim)) {
                    rowAt(row, i).* = psx.conv(i16, p);
                    return;
                }
            } else if (!(lim < x1)) {
                rowAt(row, i +% 1).* = psx.conv(i16, p);
                return;
            }
            j = @as(c_int, count.*) -% 1;
            while (i < j) : (j -%= 1) rowAt(row, j +% 2).* = rowAt(row, j).*;
            count.* +%= 2;
            if (mode == 0) {
                rowAt(row, j +% 1).* = psx.conv(i16, lim);
                rowAt(row, i +% 2).* = psx.conv(i16, p);
            } else {
                rowAt(row, j +% 2).* = psx.conv(i16, lim);
                rowAt(row, i +% 1).* = psx.conv(i16, p);
            }
            return;
        }
        i +%= 2;
        if (!(i < count.*)) return;
    }
}

/// `spans + row * 32`: the row's span words.
fn spanRow(spans: [*]i16, row: c_int) [*]i16 {
    return @ptrCast(psx.at(spans, row *% 32));
}

/// orig: func_800C2E24 (pc/src/map_view.c)
/// Draws the area Map screen: the 9x9 cell outlines projected into per-scanline
/// spans, cut by walls and marker triangles, clipped to a 32-point circle and
/// emitted as LINE_F2 packets along its right edge.
pub fn drawAreaMapScreen(origin: *c.SVECTOR, m: *c.MATRIX, scale: c_int) callconv(.c) void {
    const state: [*]u8 = @ptrFromInt(@as(usize, v.g.D_800EB9B8().*));
    const grid: [*]u8 = @ptrFromInt(scratch_base + 0x88);
    const rows: [*]u8 = @ptrFromInt(scratch_base + 0xdc);
    const edges: [*]u8 = @ptrFromInt(scratch_base + 0x1bc);
    const cells: [*]c_uint = @ptrCast(@alignCast(state + 0x4370));
    const spans: [*]i16 = @ptrCast(@alignCast(state + 0xb70));
    const points: [*]c_uint = @ptrCast(@alignCast(edges));
    const grid_words: [*]c_uint = @ptrCast(@alignCast(grid));
    var packet: [*]c_uint = undefined;
    const y: c_int = origin.vy;
    var k: c_int = undefined;
    var row: c_int = undefined;
    var col: c_int = undefined;
    var top: c_int = undefined;
    var top_index: c_int = undefined;
    var index: c_int = undefined;
    var i: c_int = undefined;

    i = 0x14;
    while (i >= 0) : (i -%= 1) grid_words[@intCast(i)] = 0x01010101;
    grid[0x53] = 0;
    i = 0x15;
    while (i < 0x7a) : (i +%= 1) grid_words[@intCast(i)] = 0;

    k = 0;
    const view_turn: c_int = psx.byteAt(i16, state, 0x22).*;
    switch (((view_turn -% 0x200) >> 10) & 3) {
        0 => {
            row = 0;
            while (row < 0x49) : (row +%= 9) {
                col = 0;
                while (col < 9) : (col +%= 1) {
                    mapCell(psx.at(grid, k), cells[@intCast(col +% row)], y);
                    k +%= 1;
                }
            }
        },
        1 => {
            col = 0;
            while (col < 9) : (col +%= 1) {
                row = 0x48;
                while (row >= 0) : (row -%= 9) {
                    mapCell(psx.at(grid, k), cells[@intCast(col +% row)], y);
                    k +%= 1;
                }
            }
        },
        2 => {
            row = 0x48;
            while (row >= 0) : (row -%= 9) {
                col = 8;
                while (col >= 0) : (col -%= 1) {
                    mapCell(psx.at(grid, k), cells[@intCast(col +% row)], y);
                    k +%= 1;
                }
            }
        },
        else => {
            col = 8;
            while (col >= 0) : (col -%= 1) {
                row = 0;
                while (row < 0x49) : (row +%= 9) {
                    mapCell(psx.at(grid, k), cells[@intCast(col +% row)], y);
                    k +%= 1;
                }
            }
        },
    }

    // Toggle the four edges of every empty cell (19-byte edge rows).
    k = 0;
    row = 0;
    while (row < 0x99) : (row +%= 0x13) {
        col = 0;
        while (col < 9) : (col +%= 1) {
            const cell = grid[@intCast(k)];
            k +%= 1;
            if (cell == 0) {
                const e: [*]u8 = @ptrCast(psx.at(edges, col +% row));
                e[0] = psx.conv(u8, @as(c_int, 1) -% e[0]);
                e[9] = psx.conv(u8, @as(c_int, 1) -% e[9]);
                e[10] = psx.conv(u8, @as(c_int, 1) -% e[10]);
                e[0x13] = psx.conv(u8, @as(c_int, 1) -% e[0x13]);
            }
        }
    }

    // Project the remaining edges at height y and add them to the spans.
    const y_hi: c_uint = @as(c_uint, @bitCast(y)) << 16;
    const origin_x: c_int = origin.vx;
    const origin_z: c_int = origin.vz;
    const base_x: c_int = ((origin_x >> 7) -% 4) << 7;
    const base_z: c_int = ((origin_z >> 7) -% 4) << 7;
    const rotation: c_int = ((view_turn -% 0x200) >> 10) & 3;
    const outline: [*]u16 = @ptrCast(@alignCast(state + @as(usize, @intCast(0x44b4 +% rotation *% 0x168))));
    i = 0;
    while (i < 0xb4) : (i +%= 1) {
        const e = psx.at(edges, i);
        if (e.* == 0) continue;
        const t0: c_uint = outline[@intCast(i)];
        var t2: c_int = @bitCast(mapRtps(((@as(c_uint, @bitCast(base_x)) +% (t0 & 0xf) *% 128) & 0xffff) | y_hi, @bitCast(@as(c_uint, @bitCast(base_z)) +% ((t0 << 3) & 0x780))));
        const z2: c_int = @bitCast(((t0 >> 5) & 0x780) +% @as(c_uint, @bitCast(base_z)));
        const xy2: c_uint = ((@as(c_uint, @bitCast(base_x)) +% ((t0 >> 1) & 0x780)) & 0xffff) | y_hi;
        var t1: c_int = t2 >> 16;
        t2 = @bitCast(@as(c_uint, @bitCast(t2)) << 16);
        var t3: c_int = @bitCast(mapRtps(xy2, z2));
        e.* = 0;
        t2 +%= 0x8000;
        var t5: c_int = t3 >> 16;
        t3 = @bitCast((@as(c_uint, @bitCast(t3)) << 16) | 0x8000);
        const e_b4 = psx.at(e, 0xb4);
        if (@as(c_uint, @bitCast(t1 -% t5 +% 2)) < 5) {
            e.* = if (t3 < t2) 0xff else 0x7f;
            e_b4.* = psx.conv(u8, t1);
        }
        if (t1 < t5) {
            if (t5 <= 0 or !(t1 < 0xe0)) continue;
            const r: c_int = if (t1 < 0) 0 else t1;
            if (e.* == 0) {
                const cnt: c_int = psx.at(rows, r).*;
                if (if (t3 -% t2 < 0) (cnt & 1) != 0 else ((cnt +% 1) & 1) != 0) e.* = psx.conv(u8, @as(c_int, psx.at(rows, r).*) +% 1);
            }
            const step = mapStep(t3 -% t2, t5 -% t1);
            if (t5 >= 0xe1) t5 = 0xe0;
            e_b4.* = psx.conv(u8, t5);
            while (t1 < 0) {
                t1 +%= 1;
                t2 +%= step;
            }
            while (true) {
                const cp = psx.at(rows, t1);
                const n: c_int = cp.*;
                const x = t2 >> 16;
                t2 +%= step;
                cp.* = psx.conv(u8, n +% 1);
                psx.at(spans, t1 *% 32 +% n).* = psx.conv(i16, x);
                t1 +%= 1;
                if (!(t1 < t5)) break;
            }
        } else if (t5 < t1) {
            if (t1 <= 0 or !(t5 < 0xe0)) continue;
            const r: c_int = if (t5 < 0) 0 else t5;
            if (e.* == 0) {
                const cnt: c_int = psx.at(rows, r).*;
                if (if (t2 -% t3 < 0) (cnt & 1) != 0 else ((cnt +% 1) & 1) != 0) e.* = psx.conv(u8, @as(c_int, psx.at(rows, r).*) +% 0x81);
            }
            const step = mapStep(t2 -% t3, t1 -% t5);
            if (t1 >= 0xe1) t1 = 0xe0;
            e_b4.* = psx.conv(u8, t1);
            while (t5 < 0) {
                t5 +%= 1;
                t3 +%= step;
            }
            while (true) {
                const cp = psx.at(rows, t5);
                const n: c_int = cp.*;
                const x = t3 >> 16;
                t3 +%= step;
                cp.* = psx.conv(u8, n +% 1);
                psx.at(spans, t5 *% 32 +% n).* = psx.conv(i16, x);
                t5 +%= 1;
                if (!(t5 < t1)) break;
            }
        }
    }

    // Walls: project inner edges lowered by the two neighbouring cell heights
    // and cut the spans between the lowered and the original edge.
    i = 10;
    while (i < 0xaa) : (i +%= 1) {
        const e = psx.at(edges, i);
        const a2: c_int = e.*;
        if (a2 == 0) continue;
        const pair: c_uint = v.g.D_800EA6B0()[@intCast(i)];
        var t9: c_int = @as(c_int, psx.at(grid, pair & 0xff).*) +% @as(c_int, psx.at(grid, pair >> 8).*);
        if (t9 < 2) continue;
        var t0: c_uint = outline[@intCast(i)];
        if (a2 & 0x80 != 0) t0 = ((t0 >> 8) | (t0 << 8)) & 0xffff;
        const y_lo: c_uint = @as(c_uint, @bitCast(y -% t9)) << 16;
        var t2: c_int = @bitCast(mapRtps(((@as(c_uint, @bitCast(base_x)) +% (t0 & 0xf) *% 128) & 0xffff) | y_lo, @bitCast(@as(c_uint, @bitCast(base_z)) +% ((t0 << 3) & 0x780))));
        const z2: c_int = @bitCast(((t0 >> 5) & 0x780) +% @as(c_uint, @bitCast(base_z)));
        const xy2: c_uint = ((@as(c_uint, @bitCast(base_x)) +% ((t0 >> 1) & 0x780)) & 0xffff) | y_lo;
        var t1: c_int = t2 >> 16;
        t2 = @bitCast(@as(c_uint, @bitCast(t2)) << 16);
        var t3: c_int = @bitCast(mapRtps(xy2, z2));
        t9 = t2 >> 16;
        t2 +%= 0x8000;
        var t5: c_int = t3 >> 16;
        if (t5 <= 0) continue;
        t3 = @bitCast((@as(c_uint, @bitCast(t3)) << 16) | 0x8000);
        const e_b4 = psx.at(e, 0xb4);
        if ((a2 & 0x7f) == 0x7f) {
            t2 = t9;
            t5 = e_b4.*;
            t3 >>= 16;
            if (!(t5 < 0xe0)) t5 = 0xe0;
            if (t1 < 0) t1 = 0;
            while (t1 < t5) : (t1 +%= 1) mapCut(psx.at(rows, t1), spanRow(spans, t1), t2, t3);
            continue;
        }
        const step = mapStep(t3 -% t2, t5 -% t1); // t5 - t1 may be negative
        if (t5 >= 0xe1) t5 = 0xe0;
        while (t1 < 0) {
            t1 +%= 1;
            t2 +%= step;
        }
        if (a2 & 1 != 0) {
            while (t1 < t5) : ({
                t1 +%= 1;
                t2 +%= step;
            }) mapCutWall(psx.at(rows, t1), spanRow(spans, t1), t2 >> 16, t9, 0);
            t1 = e_b4.*;
            t2 >>= 16;
            while (t5 < t1) : (t5 +%= 1) mapCutWall(psx.at(rows, t5), spanRow(spans, t5), t2, t9, 0);
        } else {
            while (t1 < t5) : ({
                t1 +%= 1;
                t2 +%= step;
            }) mapCutWall(psx.at(rows, t1), spanRow(spans, t1), t2 >> 16, t9, 1);
            t1 = e_b4.*;
            t2 >>= 16;
            while (t5 < t1) : (t5 +%= 1) mapCutWall(psx.at(rows, t5), spanRow(spans, t5), t2, t9, 2);
        }
    }

    // Marker triangles from the 9x9 grid at +0x4AB8: rasterise and cut.
    row = 0;
    while (row < 9) : (row +%= 1) {
        col = 0;
        while (col < 9) : (col +%= 1) {
            const a2: c_int = state[@intCast(0x4ab8 +% row *% 9 +% col)];
            if (a2 == 0) continue;
            const bits: c_int = @as(c_int, 0x38847780) >> @intCast(a2 & 31);
            const cx: c_int = (origin_x >> 7) +% col -% 4;
            const cz: c_int = (origin_z >> 7) +% row -% 4;
            c.MTC2(@as(c_uint, @bitCast(((cx +% (bits & 1)) << 7) & 0xffff)) | y_hi, 0);
            c.MTC2(@bitCast((cz +% ((bits >> 1) & 1)) << 7), 1);
            c.MTC2(@as(c_uint, @bitCast(((cx +% ((bits >> 2) & 1)) << 7) & 0xffff)) | y_hi, 2);
            c.MTC2(@bitCast((cz +% ((bits >> 3) & 1)) << 7), 3);
            c.MTC2(@as(c_uint, @bitCast(((cx +% ((bits >> 4) & 1)) << 7) & 0xffff)) | y_hi, 4);
            c.MTC2(@bitCast((cz +% ((bits >> 5) & 1)) << 7), 5);
            gte.rtpt();
            points[0] = c.MFC2(12);
            points[1] = c.MFC2(13);
            points[2] = c.MFC2(14);
            points[3] = points[0];
            top = @as(i16, @bitCast(@as(u16, @truncate(points[0] >> 16))));
            var bottom: c_int = top;
            k = @as(i16, @bitCast(@as(u16, @truncate(points[1] >> 16))));
            if (k < top) top = k;
            if (bottom < k) bottom = k;
            k = @as(i16, @bitCast(@as(u16, @truncate(points[2] >> 16))));
            if (k < top) top = k;
            if (bottom < k) bottom = k;
            if (top < 0) top = 0;
            if (bottom >= 0xe1) bottom = 0xe0;
            var edge: usize = 0;
            while (edge < 3) : (edge += 1) { // edge < 3
                var t1: c_int = @as(c_int, @bitCast(points[edge])) >> 16;
                var t2: c_int = @bitCast((points[edge] << 16) +% 0x8000);
                var t3: c_int = @bitCast((points[edge + 1] << 16) +% 0x8000);
                var t5: c_int = @as(c_int, @bitCast(points[edge + 1])) >> 16;
                var step: c_int = undefined;
                if (t1 < t5) {
                    step = mapStep(t3 -% t2, t5 -% t1);
                    if (t5 >= 0xe1) t5 = 0xe0;
                    while (t1 < 0) {
                        t1 +%= 1;
                        t2 +%= step;
                    }
                    while (t1 < t5) : ({
                        t1 +%= 1;
                        t2 +%= step;
                    }) mapRow(0x12 +% (t1 -% top) *% 4)[0] = psx.conv(i16, t2 >> 16);
                } else {
                    step = mapStep(t2 -% t3, t1 -% t5);
                    if (t1 >= 0xe1) t1 = 0xe0;
                    while (t5 < 0) {
                        t5 +%= 1;
                        t3 +%= step;
                    }
                    while (t5 < t1) : ({
                        t5 +%= 1;
                        t3 +%= step;
                    }) mapRow(0x10 +% (t5 -% top) *% 4)[0] = psx.conv(i16, t3 >> 16);
                }
            }
            k = top;
            while (k < bottom) : (k +%= 1) {
                const bounds = mapRow(0x10 +% (k -% top) *% 4);
                mapCut(psx.at(rows, k), spanRow(spans, k), bounds[0], bounds[1]);
            }
        }
    }

    // 32-point circle of radius `scale` around the origin, in camera space
    // (SXY, SZ >> 2 per point); remember the topmost on-screen point.
    {
        const ax: c_int = (@as(c_int, m.m[0][0]) *% scale +% 0x800) >> 12;
        const az: c_int = (@as(c_int, m.m[2][0]) *% scale +% 0x800) >> 12;
        const bx: c_int = (@as(c_int, m.m[0][2]) *% scale +% 0x800) >> 12;
        const bz: c_int = (@as(c_int, m.m[2][2]) *% scale +% 0x800) >> 12;
        top = 0xe0;
        top_index = 9; // stale $s2 from the triangle loop when nothing is above row 0xE0
        i = 0;
        while (i < 32) : (i +%= 1) {
            const cc: c_int = v.g.D_800EA818()[@intCast(i +% 8)];
            const ss: c_int = v.g.D_800EA818()[@intCast(i)];
            const dx: c_int = (ax *% cc +% az *% ss +% 0x800) >> 12;
            const dz: c_int = (bx *% cc +% bz *% ss +% 0x800) >> 12;
            points[@intCast(i *% 2)] = mapRtps((@as(c_uint, @bitCast(origin_x +% dx)) & 0xffff) | y_hi, origin_z +% dz);
            points[@intCast(i *% 2 +% 1)] = @bitCast(@as(c_int, @bitCast(c.MFC2(19))) >> 2);
            const sy: c_int = @as(i16, @bitCast(@as(u16, @truncate(points[@intCast(i *% 2)] >> 16))));
            if (sy < top) {
                top = sy;
                top_index = i;
            }
        }
    }

    // Walk the circle's left side downwards, clipping span ends to it.
    index = (top_index -% 1) & 31;
    while (true) {
        var t2: c_int = @bitCast(points[@intCast(((index +% 1) & 31) *% 2)]);
        var t3: c_int = @bitCast(points[@intCast(index *% 2)]);
        var t1: c_int = t2 >> 16;
        var t5: c_int = t3 >> 16;
        if (t1 == t5 or t5 < 0) {
            if (index == top_index) break;
            index = (index -% 1) & 31;
            continue;
        }
        if (t5 < t1 or !(t1 < 0xe0)) break;
        t2 = @bitCast((@as(c_uint, @bitCast(t2)) << 16) +% 0x8000);
        t3 = @bitCast((@as(c_uint, @bitCast(t3)) << 16) +% 0x8000);
        const step = mapStep(t3 -% t2, t5 -% t1);
        if (t5 >= 0xe1) t5 = 0xe0;
        if (t1 < 0) { // the original advances only one row here
            t1 +%= 1;
            t2 +%= step;
        }
        if (t1 < -0xdc) c.pc_unimplemented("map circle clip above scratchpad");
        while (true) {
            const cp = psx.at(rows, t1);
            var n: c_int = cp.*;
            const x = t2 >> 16;
            if (n > 0) {
                const line = spanRow(spans, t1);
                while (true) {
                    if (@as(c_int, rowAt(line, n -% 2).*) < x) {
                        if (x < @as(c_int, rowAt(line, n -% 1).*)) rowAt(line, n -% 1).* = psx.conv(i16, x);
                        break;
                    }
                    n -%= 2;
                    if (n <= 0) break;
                }
            }
            cp.* = psx.conv(u8, n);
            t1 +%= 1;
            t2 +%= step;
            if (!(t1 < t5)) break;
        }
        index = (index -% 1) & 31;
    }

    // Walk the right side downwards and emit the spans left of it.
    packet = @ptrFromInt(@as(usize, @as(*const c_uint, @ptrFromInt(scratch_base)).*));
    const first_packet = packet;
    v.g.pc_map_emitted_count().* = 0;
    index = (top_index +% 1) & 31;
    while (true) {
        const p0 = points + @as(usize, @intCast(((index -% 1) & 31) *% 2));
        const p1 = points + @as(usize, @intCast(index *% 2));
        var t2: c_int = @bitCast(p0[0]);
        var t3: c_int = @bitCast(p1[0]);
        var t1: c_int = t2 >> 16;
        var t5: c_int = t3 >> 16;
        if (t1 == t5 or t5 < 0) {
            if (index == top_index) break;
            index = (index +% 1) & 31;
            continue;
        }
        if (t5 < t1 or !(t1 < 0xe0)) break;
        var z: c_int = @bitCast((@as(c_uint, @as(u16, @truncate(p0[1]))) << 16) +% 0x8000);
        var z_step: c_int = @bitCast((@as(c_uint, @as(u16, @truncate(p1[1]))) << 16) +% 0x8000);
        const recip = mapRecip(t5 -% t1);
        z_step = ((z_step -% z) >> 15) *% recip;
        t2 = @bitCast((@as(c_uint, @bitCast(t2)) << 16) +% 0x8000);
        t3 = @bitCast((@as(c_uint, @bitCast(t3)) << 16) +% 0x8000);
        const step = ((t3 -% t2) >> 15) *% recip;
        if (t5 >= 0xe1) t5 = 0xe0;
        while (t1 < 0) {
            t1 +%= 1;
            t2 +%= step;
            z +%= z_step;
        }
        while (true) {
            const depth: c_int = z >> 16;
            if (@as(c_uint, @bitCast(depth)) < 0x800) {
                const color: c_uint = @as(c_uint, @bitCast((t1 >> 1) << 8)) | 0x42000000;
                const ot: [*]c_uint = @ptrFromInt(@as(usize, @as(*const c_uint, @ptrFromInt(scratch_base + 4)).*));
                const ot_slot = psx.at(ot, depth);
                const x = t2 >> 16;
                var n: c_int = psx.at(rows, t1).*;
                if (n > 0) {
                    const line = spanRow(spans, t1);
                    const row_y: c_uint = @as(c_uint, @bitCast(t1)) << 16;
                    const xy0: c_uint = (@as(c_uint, @bitCast(x)) & 0xffff) | row_y;
                    while (true) {
                        const w: c_uint = @as(*align(1) const c_uint, @ptrCast(rowAt(line, n -% 2))).*;
                        const x0: c_int = @as(i16, @bitCast(@as(u16, @truncate(w))));
                        const x1: c_int = @as(c_int, @bitCast(w)) >> 16;
                        if (x0 < x1) {
                            var from: c_uint = (w & 0xffff) | row_y;
                            var last = false;
                            if (x0 < x) {
                                if (!(x < x1)) break;
                                from = xy0;
                                last = true;
                            }
                            packet[0] = (ot_slot.* & 0xffffff) | 0x04000000;
                            packet[1] = 0xe1000220;
                            packet[2] = color;
                            packet[3] = from;
                            packet[4] = (w >> 16) | row_y;
                            const emitted_count = v.g.pc_map_emitted_count();
                            if (emitted_count.* < 1024) {
                                const entry = &v.g.pc_map_emitted()[@intCast(emitted_count.*)]; // < PC_MAP_EMITTED
                                entry.packet = @ptrCast(packet);
                                entry.row = psx.conv(i16, t1);
                                entry.x0 = psx.conv(i16, from);
                                entry.x1 = psx.conv(i16, w >> 16);
                                emitted_count.* +%= 1;
                            }
                            ot_slot.* = psx.addr(packet) & 0xffffff;
                            packet += 5;
                            if (last) break;
                        }
                        n -%= 2;
                        if (!(n > 0)) break;
                    }
                }
            }
            t1 +%= 1;
            t2 +%= step;
            z +%= z_step;
            if (!(t1 < t5)) break;
        }
        index = (index +% 1) & 31;
    }
    @as(*c_uint, @ptrFromInt(scratch_base)).* = psx.addr(packet);
    // Widescreen: the spans are made from the narrowed projections: drawn
    // where they are (gpu_hw.c), not compressed as screen furniture.
    if (psx.weak(AnchorPackets, "pc_hw_anchor_packets")) |anchor| anchor(first_packet, packet, c.PC_ANCHOR_PROJECTED);
    mapSpanShapes();
}
