//! Particle node renderer (D2: 6E644.s; oracle pc/src/effect_particles.c).
const c = @import("c_effect_particles");
const v = @import("views_effect_particles");
const psx = @import("common").psx;
const gte = @import("common").gte;
const sprite_item = @import("common").sprite_item;

const NativeBegin = *const fn () callconv(.c) void;
const NativeTakes = *const fn (c_int, c_int, c_int, c_int) callconv(.c) c_int;
const NativeNode = *const fn (c_int, [*]const c_int, [*]const c_int, [*]const c_int, [*]const c_int, [*c]const c_int, c_int, c_uint, c_int, ?*const anyopaque, c_int, c_uint) callconv(.c) void;
const NativeItems = *const fn ([*c]c_uint, [*c]c_uint) callconv(.c) void;
/// The most tiles of a node the native list takes (effect_native.c: the same limit).
const max_node_tiles = 32;
const NativeEnd = *const fn ([*c]c_uint, [*c]c_uint) callconv(.c) [*c]c_uint;

/// A tile corner: the x/y word at `offset`, z zero (the sprite's rotation has no z terms).
fn cornerAt(tile: [*]u8, offset: usize) [3]i16 {
    const word = @as(*align(1) c_uint, @ptrCast(tile + offset)).*;
    return .{ psx.conv(i16, word), psx.conv(i16, word >> 16), 0 };
}

/// orig: pc_curve_value (pc/src/effect_particles.c)
/// Sample `clock` of colour curve `index` (an empty curve is read at `clock`, as `div $zero` leaves HI).
fn curveValue(sizes: [*]const u8, index: c_uint, clock: c_uint) c_uint {
    if (index >= 32) c.pc_unimplemented("invalid particle color curve");
    const samples: [*]const u8 = @ptrCast(psx.get(psx.at(&v.g.vs_effects_package().*.?.curves, index).*).?); // curves[32]: unchecked past 32 (above)
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

/// orig: vs_effects_drawAnimatedSpriteParticles (pc/src/effect_particles.c)
/// Draws the particle list `head`: steps each node's sprite script, projects
/// it and emits its tiles as sprite packets into the ordering table.
pub fn drawParticles(head: ?*v.func_800D6E44_t) callconv(.c) void {
    var node_ptr = head;
    const curve_block = psx.get(v.g.vs_effects_package().*.?.curveBlock);
    // &block->curveSizes: an address only, the block is not read here.
    const curve_sizes: [*]const u8 = @ptrFromInt(@intFromPtr(curve_block) +% @offsetOf(v.pFileCurveBlock, "curveSizes"));
    const projection: c_int = v.g.vs_main_projectionDistance().* << 10;
    const parent = v.g.vs_effects_currentInstance().*;
    var a: c_int = undefined;
    var b: c_int = undefined;

    // The light matrix's last rows (registers 11 and 12) are constant: the sprite's rotation is a turn in the plane.
    var light: [5]c_uint = .{ 0, 0, 0, 0, 0x1000 };
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
                        const table = psx.get(psx.at(&v.g.vs_effects_package().*.?.block1Tables, node.sprite.unk16).*);
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
        var projView: gte.View = undefined; // the matrices the registers held for this node's projections
        if (node.flags & 0x20000 != 0) {
            if (node.flags & 0x80000 != 0) {
                const m = &psx.at(&parent.?.unk1C, node.matrixIndex).unk38;
                const attached = gte.viewOf(m, gte.screenCurrent());
                b = z;
                const moved = gte.transform(&attached, &[3]i16{ psx.conv(i16, a), psx.conv(i16, a >> 16), psx.conv(i16, b) });
                b = moved.mac[2];
                if (b > 0) continue;
            }
            const m = &psx.at(&parent.?.unk1C, node.matrixIndex).unk58;
            projView = gte.viewOf(m, gte.screenCurrent());
        } else {
            if ((node.flags & 0x80000) != 0 and y > 0) continue;
            projView = gte.viewOf(&v.g.vs_view().viewMatrix, gte.screenCurrent());
        }
        b = z;
        const centred = gte.project(&projView, &[3]i16{ psx.conv(i16, a), psx.conv(i16, a >> 16), psx.conv(i16, b) });
        a = centred.sxy;
        var otz: c_int = @bitCast(centred.sz);
        // The centre in view space and its depth (native particles).
        const view: [3]c_int = .{ centred.mac[1], centred.mac[2], centred.mac[3] };
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
        if (node.flags & 0x10000 != 0) {
            x = ((x +% (node.dx >> 5)) & 0xFFFF) +% ((y +% (node.dy >> 5)) << 16);
            z +%= node.dz >> 5;
            x = gte.project(&projView, &[3]i16{ psx.conv(i16, x), psx.conv(i16, x >> 16), psx.conv(i16, z) }).sxy;
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
                    cc = psx.at(v.g.vs_core_mathArcTangentTable(), cc).*;
                    cc = t -% cc;
                } else {
                    cc = ay << 9;
                    cc = @divTrunc(cc, ax);
                    cc = psx.at(v.g.vs_core_mathArcTangentTable(), cc).*;
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
        const background: [3]c_uint = .{ @bitCast(short(a)), @bitCast(a >> 16), 0 }; // BK: the centre's screen position
        a >>= 16;
        const indices = psx.get(node.sprite.unk1C).?;
        // The node's tiles go to the native list when it takes them (no packets, no items: it reads the tiles' words); otherwise
        // they are draw items (one marker each at its slot, where the packets linked), the markers' range [start, end) noted after.
        const layered = mode >= 5 and mode <= 8;
        const native = if (psx.weak(NativeTakes, "pc_particles_native_takes")) |takes|
            takes(indices.unk0, depth, @intFromBool(layered), centre_sz) != 0
        else
            false;
        const node_start: [*c]c_uint = @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk0).?));
        var page_clut: [max_node_tiles]c_int = undefined;
        var tile_rgb: [max_node_tiles]c_int = undefined;
        var tile_xy: [max_node_tiles * 4]c_int = undefined;
        var tile_uv: [max_node_tiles * 4]c_int = undefined;
        // block1Data + dataIndex: an address only until the tiles are read.
        var tile: [*]u8 = @ptrFromInt(@intFromPtr(psx.get(v.g.vs_effects_package().*.?.block1Data)) +% @as(usize, indices.dataIndex) * @sizeOf(v.pFileBlock1Data));
        a = projection_scale;
        const scale: c_int = psx.getBits(&node.unk60.bits, "scale");
        b = a *% (@divTrunc(c.rcos(node.sprite.unk14) *% scale, 4));
        var sinv: c_int = @divTrunc(c.rsin(node.sprite.unk14) *% scale, 4);
        b >>= 12;
        b &= 0xFFFF;
        sinv *%= a;
        sinv >>= 12;
        a = b | (-%sinv << 16);
        light[0] = @bitCast(a);
        sinv <<= 16;
        light[1] = @bitCast(sinv);
        light[2] = @bitCast(b);
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
            var xy: [4]c_int = undefined;
            var uv: [4]c_int = undefined;
            y = @as(*align(1) c_int, @ptrCast(tile)).*;
            z = @as(*align(1) u16, @ptrCast(tile + 4)).*; // &tile->u0
            const size: c_int = @as(*align(1) u16, @ptrCast(tile + 6)).*; // &tile->u1
            // gte_mvmva(1, 1, vector, 1, 0) on each corner: the light matrix and the background colour (the corners' z is not
            // used: the matrix's z terms are zero).
            const c0 = gte.lightTransform(&light, &background, &cornerAt(tile, 8));
            uv[2] = psx.conv(u16, z +% (size & 0xFF00));
            uv[1] = (z +% (size & 0xFF)) & 0xFFFF;
            xy[0] = (@as(c_int, c0[0]) & 0xFFFF) +% (@as(c_int, c0[1]) << 16);
            const c1 = gte.lightTransform(&light, &background, &cornerAt(tile, 12));
            uv[3] = psx.conv(u16, z +% size);
            xy[1] = (@as(c_int, c1[0]) & 0xFFFF) +% (@as(c_int, c1[1]) << 16);
            const c2 = gte.lightTransform(&light, &background, &cornerAt(tile, 16));
            xy[2] = (@as(c_int, c2[0]) & 0xFFFF) +% (@as(c_int, c2[1]) << 16);
            uv[0] = z & 0xFFFF;
            const c3 = gte.lightTransform(&light, &background, &cornerAt(tile, 20)); // &tile->unk8[12]
            tile += @sizeOf(v.pFileBlock1Data);
            xy[3] = (@as(c_int, c3[0]) & 0xFFFF) +% (@as(c_int, c3[1]) << 16);
            // The tile: a textured quad with its page (the high half of its first word) and CLUT, semi-transparent by the CLUT's top
            // bit; the colour word's command byte is the packet's (the item's colour is its low three bytes).
            if (native) {
                const k: usize = @intCast(index);
                page_clut[k] = y;
                tile_rgb[k] = rgb;
                @memcpy(tile_xy[k * 4 ..][0..4], &xy);
                @memcpy(tile_uv[k * 4 ..][0..4], &uv);
            } else {
                const item = sprite_item.ft4(@ptrCast(otSlot(depth)), false, y & 0x8000 != 0, y >> 16, y & 0xFFFF, rgb, &xy, &uv);
                sprite_item.bracketed(item);
            }
        }
        if (native) {
            if (psx.weak(NativeNode, "pc_particles_native_node")) |native_node| {
                // The node's particle system for glows (glows.c): its sprite script and table, and the matrix it is attached to.
                const attached = node.flags & 0x20000 != 0;
                const source: c_uint = (psx.rawOf(node.sprite.unk18) *% 2654435761) ^ (@as(c_uint, node.sprite.unk16) << 8) ^
                    (if (attached) 0x10000 +% @as(c_uint, node.matrixIndex) else 0);
                native_node(indices.unk0, &page_clut, &tile_rgb, &tile_xy, &tile_uv, &view, centre_sz, centre_xy, depth, node, node.curves.fields.age, (source & 0x07ffffff) | (if (attached) @as(c_uint, 0x08000000) else 0) | 1); // attached: glows.c
            }
        } else if (psx.weak(NativeItems, "pc_particles_native_items")) |items| {
            items(node_start, @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk0).?)));
        }
    }
    if (psx.weak(NativeEnd, "pc_particles_native_end")) |native_end| {
        const end = native_end(@ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk0).?)), @ptrCast(@alignCast(psx.get(v.g.vs_scratch().unk4).?)));
        psx.put(&v.g.vs_scratch().unk0, @as(?*anyopaque, @ptrCast(end)));
    }
}
