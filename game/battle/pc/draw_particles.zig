//! Particle node renderer (D2: 6E644.s; oracle pc/src/effect_particles.c).
const c = @import("c_effect_particles");
const v = @import("views_effect_particles");
const psx = @import("common").psx;
const gte = @import("common").gte;

const NativeBegin = *const fn () callconv(.c) void;
const NativeNode = *const fn ([*c]c_uint, [*c]c_uint, c_int, [*c]const c_int, c_int, c_uint, c_int, c_int, ?*const anyopaque, c_int, c_uint) callconv(.c) void;
const NativeEnd = *const fn ([*c]c_uint, [*c]c_uint) callconv(.c) [*c]c_uint;

/// gte_mvmva(1, 1, vector, 1, 0): the sprite's rotation applied to V0..V2.
const mvmva_v0 = 0x0400012 | (1 << 19) | (1 << 17) | (0 << 15) | (1 << 13);
const mvmva_v1 = 0x0400012 | (1 << 19) | (1 << 17) | (1 << 15) | (1 << 13);
const mvmva_v2 = 0x0400012 | (1 << 19) | (1 << 17) | (2 << 15) | (1 << 13);

/// orig: pc_curve_value (pc/src/effect_particles.c)
/// Sample `clock` of colour curve `index` (an empty curve is read at `clock`, as `div $zero` leaves HI).
fn curveValue(sizes: [*]const u8, index: c_uint, clock: c_uint) c_uint {
    if (index >= 32) c.pc_unimplemented("invalid particle color curve");
    const samples: [*]const u8 = @ptrCast(psx.get(psx.at(&v.g.D_800F569C().*.?.curves, index).*).?); // curves[32]: unchecked past 32 (above)
    const size: c_uint = psx.at(sizes, index).*;
    return psx.at(samples, if (size != 0) clock % size else clock).*;
}

fn otSlot(depth: c_int) *c_uint {
    const ot: [*]c_uint = @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk4).?));
    return psx.at(ot, depth);
}

fn short(value: anytype) c_int {
    return psx.conv(i16, value);
}

/// orig: func_800D6E44 (pc/src/effect_particles.c)
/// Draws the particle list `head`: steps each node's sprite script, projects
/// it and emits its tiles as sprite packets into the ordering table.
pub fn drawParticles(head: ?*v.func_800D6E44_t) callconv(.c) void {
    var node_ptr = head;
    const curve_block = psx.get(v.g.D_800F569C().*.?.curveBlock);
    // &block->curveSizes: an address only, the block is not read here.
    const curve_sizes: [*]const u8 = @ptrFromInt(@intFromPtr(curve_block) +% @offsetOf(v.pFileCurveBlock, "curveSizes"));
    const projection: c_int = v.g.vs_main_projectionDistance().* << 10;
    const parent = v.g.D_800F53BC().*;
    var a: c_int = undefined;
    var b: c_int = undefined;

    c.CTC2(0, 11);
    c.CTC2(0x1000, 12);
    if (psx.weak(NativeBegin, "pc_particles_native_begin")) |begin| begin();

    while (node_ptr) |node| : (node_ptr = psx.get(node.next)) {
        script: {
            if (node.sprite.unk6 != 0) {
                node.sprite.unk6 -%= 2;
                if (node.sprite.unk6 != 0) break :script;
            }
            while (true) {
                const cmd: c_int = @as(*align(1) c_int, @ptrFromInt(@intFromPtr(psx.get(node.sprite.unk18)) +% node.sprite.unkE)).*;
                switch (@as(c_uint, @bitCast(cmd)) & 0xF0000000) {
                    0 => {
                        const table = psx.get(psx.at(&v.g.D_800F569C().*.?.block1Tables, node.sprite.unk16).*);
                        psx.put(&node.sprite.unk1C, @as(*v.pFileBlock1Indices, @ptrFromInt(@intFromPtr(table) +% @as(usize, @intCast(cmd & 0xFFFF)) * @sizeOf(v.pFileBlock1Indices))));
                        @as(*align(1) i16, @ptrCast(&node.sprite.unk6)).* = psx.conv(i16, (cmd >> 16) & 0x3FFF);
                        node.sprite.unkE +%= 4;
                        break :script;
                    },
                    0x10000000 => {
                        node.sprite.unk8 = psx.conv(i16, (cmd << 18) >> 18);
                        node.sprite.unkA = psx.conv(i16, (cmd << 4) >> 18);
                        node.sprite.unkE +%= 4;
                    },
                    0x20000000 => node.sprite.unkE = 0,
                    0x30000000 => break :script,
                    else => c.pc_unimplemented("unknown particle sprite command"),
                }
            }
        }
        var x: c_int = (node.x >> 12) +% node.sprite.unk8;
        var y: c_int = (node.y >> 12) +% node.sprite.unkA;
        var z: c_int = node.z >> 12;
        a = (x & 0xFFFF) +% (y << 16);
        if (node.flags & 0x20000 != 0) {
            if (node.flags & 0x80000 != 0) {
                const m = &psx.at(&parent.?.unk1C, node.matrixIndex).unk38;
                gte.SetRotMatrix(m);
                gte.SetTransMatrix(m);
                b = z;
                c.MTC2(@bitCast(a), 0);
                c.MTC2(@bitCast(b), 1);
                gte.rtv0tr2();
                b = @bitCast(c.MFC2(26));
                if (b > 0) continue;
            }
            const m = &psx.at(&parent.?.unk1C, node.matrixIndex).unk58;
            gte.SetRotMatrix(m);
            gte.SetTransMatrix(m);
        } else {
            if ((node.flags & 0x80000) != 0 and y > 0) continue;
            gte.SetRotMatrix(&v.g.vs_scratch().viewMatrix);
            gte.SetTransMatrix(&v.g.vs_scratch().viewMatrix);
        }
        b = z;
        c.MTC2(@bitCast(a), 0);
        c.MTC2(@bitCast(b), 1);
        gte.rtps2();
        a = @bitCast(c.MFC2(14));
        var otz: c_int = @bitCast(c.MFC2(19));
        // The centre in view space and its depth (native particles).
        const view: [3]c_int = .{ @bitCast(c.MFC2(25)), @bitCast(c.MFC2(26)), @bitCast(c.MFC2(27)) };
        const centre_sz = otz;
        const centre_xy: c_uint = @bitCast(a);
        otz >>= 2;
        const mode: c_int = if (node.unk60.value & 0xF00 != 0) @intCast((node.unk60.value >> 8) & 0xF) else node.sprite.unk7;
        var depth = otz;
        switch (mode) {
            3 => depth -%= 8,
            4 => depth -%= 16,
            1 => depth +%= 8,
            2 => depth +%= 16,
            else => {},
        }
        if (depth >= 0x800 or depth <= v.g.vs_main_nearClip().*) continue;
        switch (mode) {
            5 => depth = 0x7FE,
            6 => depth = -15,
            7 => depth = -13,
            8 => depth = -11,
            else => {},
        }
        var packet: [*]c_uint = @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk0).?));
        const node_start = packet;
        psx.put(&v.g.vs_scratch().unk0, @as(?*anyopaque, @ptrCast(packet + 3)));
        packet[0] = (otSlot(depth).* & 0xFFFFFF) | 0x1000000;
        otSlot(depth).* = (otSlot(depth).* & 0xFF000000) | (psx.addr(packet) & 0xFFFFFF);
        packet[1] = 0xE1000200;
        packet[2] = 0;
        if (node.flags & 0x10000 != 0) {
            x = ((x +% (node.dx >> 5)) & 0xFFFF) +% ((y +% (node.dy >> 5)) << 16);
            z +%= node.dz >> 5;
            c.MTC2(@bitCast(x), 0);
            c.MTC2(@bitCast(z), 1);
            gte.rtps2();
            x = @bitCast(c.MFC2(14));
            y = short(a) -% short(x);
            z = (a >> 16) -% (x >> 16);
            var cc: c_int = 0;
            var t: c_int = z | y;
            if (t != 0) {
                const ax = if (y < 0) -%y else y;
                const ay = if (z < 0) -%z else z;
                t = 512;
                if (ax < ay) {
                    cc = ax << 9;
                    cc = @divTrunc(cc, ay);
                    cc = psx.at(v.g.D_80040A14(), cc).*;
                    cc = t -% cc;
                } else {
                    cc = ay << 9;
                    cc = @divTrunc(cc, ax);
                    cc = psx.at(v.g.D_80040A14(), cc).*;
                }
                t = 1024;
                if (ax != y) cc = t -% cc;
                cc <<= 1;
                if (ay != z) cc = -%cc;
            }
            node.sprite.unk14 = psx.conv(i16, cc);
        }
        // `div $zero` has no zero check: the R3000 leaves LO = -1 for a
        // non-negative dividend and 1 for a negative one.
        const projection_scale: c_int = if (otz != 0) @divTrunc(projection, otz) else if (projection < 0) 1 else -1;
        c.CTC2(@bitCast(short(a)), 13);
        a >>= 16;
        c.CTC2(@bitCast(a), 14);
        c.CTC2(0, 15);
        const indices = psx.get(node.sprite.unk1C).?;
        // block1Data + dataIndex: an address only until the tiles are read.
        var tile: [*]u8 = @ptrFromInt(@intFromPtr(psx.get(v.g.D_800F569C().*.?.block1Data)) +% @as(usize, indices.dataIndex) * @sizeOf(v.pFileBlock1Data));
        a = projection_scale;
        const scale: c_int = psx.getBits(&node.unk60.bits, "scale");
        b = a *% (@divTrunc(c.rcos(node.sprite.unk14) *% scale, 4));
        var sinv: c_int = @divTrunc(c.rsin(node.sprite.unk14) *% scale, 4);
        b >>= 12;
        b &= 0xFFFF;
        sinv *%= a;
        sinv >>= 12;
        a = b | (-%sinv << 16);
        c.CTC2(@bitCast(a), 8);
        sinv <<= 16;
        c.CTC2(@bitCast(sinv), 9);
        c.CTC2(@bitCast(b), 10);
        var rgb: c_int = 0;
        const clock: c_int = psx.conv(u8, @as(c_int, node.curves.fields.age) +% 1);
        if (node.curves.fields.rCurve != 0) {
            var index: c_int = @as(c_int, node.curves.fields.rCurve) -% 1;
            index = @bitCast(curveValue(curve_sizes, @bitCast(index), @bitCast(clock)));
            if (index != 0) rgb = index *% 2 -% 1;
        } else {
            rgb = 0x80;
        }
        if (node.curves.fields.gCurve != 0) {
            var index: c_int = @as(c_int, node.curves.fields.gCurve) -% 1;
            index = @bitCast(curveValue(curve_sizes, @bitCast(index), @bitCast(clock)));
            if (index != 0) rgb |= (index << 9) -% 0x100;
        } else {
            rgb |= 0x8000;
        }
        if (node.curves.fields.bCurve != 0) {
            var index: c_int = @as(c_int, node.curves.fields.bCurve) -% 1;
            index = @bitCast(curveValue(curve_sizes, @bitCast(index), @bitCast(clock)));
            if (index != 0) rgb |= (index *% 2 -% 1) << 16;
        } else {
            rgb |= 0x800000;
        }
        var index: c_int = 0;
        while (index < indices.unk0) : (index +%= 1) {
            packet = @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk0).?));
            c.MTC2(@as(*align(1) c_uint, @ptrCast(tile + 8)).*, 0);
            c.MTC2(@as(*align(1) c_uint, @ptrCast(tile + 12)).*, 2);
            c.MTC2(@as(*align(1) c_uint, @ptrCast(tile + 16)).*, 4);
            y = @as(*align(1) c_int, @ptrCast(tile)).*;
            z = @as(*align(1) u16, @ptrCast(tile + 4)).*; // &tile->u0
            const size: c_int = @as(*align(1) u16, @ptrCast(tile + 6)).*; // &tile->u1
            _ = c.doCOP2(mvmva_v0);
            @as(*align(1) u16, @ptrCast(&packet[7])).* = psx.conv(u16, z +% (size & 0xFF00));
            packet[5] = @bitCast((z +% (size & 0xFF)) | ((y >> 16) << 16));
            a = @bitCast(c.MFC2(9));
            b = @bitCast(c.MFC2(10));
            packet[2] = @bitCast((a & 0xFFFF) +% (b << 16));
            _ = c.doCOP2(mvmva_v1);
            if (y & 0x8000 != 0) {
                packet[1] = @as(c_uint, @bitCast(rgb)) | 0x2E000000;
            } else {
                packet[1] = @as(c_uint, @bitCast(rgb)) | 0x2C000000;
            }
            @as(*align(1) u16, @ptrCast(&packet[9])).* = psx.conv(u16, z +% size);
            a = @bitCast(c.MFC2(9));
            b = @bitCast(c.MFC2(10));
            packet[4] = @bitCast((a & 0xFFFF) +% (b << 16));
            _ = c.doCOP2(mvmva_v2);
            psx.put(&v.g.vs_scratch().unk0, @as(?*anyopaque, @ptrCast(@as([*]c_uint, @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk0).?))) + 10)));
            packet[0] = (otSlot(depth).* & 0xFFFFFF) | 0x9000000;
            a = @bitCast(c.MFC2(9));
            b = @bitCast(c.MFC2(10));
            packet[6] = @bitCast((a & 0xFFFF) +% (b << 16));
            c.MTC2(@as(*align(1) c_uint, @ptrCast(tile + 20)).*, 0); // &tile->unk8[12]
            packet[3] = @as(c_uint, @bitCast(z & 0xFFFF)) | (@as(c_uint, @bitCast(y)) << 16);
            _ = c.doCOP2(mvmva_v0);
            tile += @sizeOf(v.pFileBlock1Data);
            otSlot(depth).* = (otSlot(depth).* & 0xFF000000) | (psx.addr(packet) & 0xFFFFFF);
            a = @bitCast(c.MFC2(9));
            b = @bitCast(c.MFC2(10));
            packet[8] = @bitCast((a & 0xFFFF) +% (b << 16));
        }
        packet = @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk0).?));
        psx.put(&v.g.vs_scratch().unk0, @as(?*anyopaque, @ptrCast(packet + 3)));
        packet[0] = (otSlot(depth).* & 0xFFFFFF) | 0x1000000;
        otSlot(depth).* = (otSlot(depth).* & 0xFF000000) | (psx.addr(packet) & 0xFFFFFF);
        packet[1] = 0xE1000000;
        packet[2] = 0;
        if (psx.weak(NativeNode, "pc_particles_native_node")) |native_node| {
            // The node's particle system for glows (glows.c): its sprite
            // script and table, and the matrix it is attached to.
            const attached = node.flags & 0x20000 != 0;
            const source: c_uint = (psx.rawOf(node.sprite.unk18) *% 2654435761) ^ (@as(c_uint, node.sprite.unk16) << 8) ^
                (if (attached) 0x10000 +% @as(c_uint, node.matrixIndex) else 0);
            native_node(node_start, @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk0).?)), indices.unk0, &view, centre_sz, centre_xy, depth, @intFromBool(mode >= 5 and mode <= 8), node, node.curves.fields.age, (source & 0x07ffffff) | (if (attached) @as(c_uint, 0x08000000) else 0) | 1); // attached: glows.c
        }
    }
    if (psx.weak(NativeEnd, "pc_particles_native_end")) |native_end| {
        const end = native_end(@ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk0).?)), @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk4).?)));
        psx.put(&v.g.vs_scratch().unk0, @as(?*anyopaque, @ptrCast(end)));
    }
}
