//! Weapon and shield polygon packets with blending modes (D2: 4B16C.s; oracle pc/src/weapon_packets.c).
const c = @import("c_weapon_packets");
const psx = @import("common").psx;

const NativeDraw = *const fn (c_int, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque, ?[*]c_uint) callconv(.c) void;

/// orig: blend_state (pc/src/weapon_packets.c)
const BlendState = struct {
    packet: [*]c_uint,
    ot: [*]c_uint,
    color: c_uint,
    tpage_hi: c_uint,
    blend: c_uint,
    clut_hi: c_uint,
};

/// orig: group_walk (pc/src/weapon_packets.c)
const GroupWalk = struct {
    counter: c_int,
    total: c_int,
    done: c_int,
    counts: [*]align(1) const u16,
};

/// orig: link_packet (pc/src/weapon_packets.c)
/// Links the packet at the head of ordering table bucket `bucket` (a word count in its top byte).
fn linkPacket(s: *BlendState, packet: [*]c_uint, bucket: c_int, words: c_uint) void {
    const head = psx.at(s.ot, bucket);
    packet[0] = (head.* & 0xffffff) | (words << 24);
    head.* = (head.* & 0xff000000) | (psx.addr(packet) & 0xffffff);
}

/// orig: next_group (pc/src/weapon_packets.c)
/// The count of the next non-empty polygon group of a section (0 when finished) and its CLUT word.
fn nextGroup(g: *GroupWalk, section_total: c_uint, mask: c_uint, clut: c_int, alternate_signed: bool, clut_hi: *c_uint) c_int {
    while (true) {
        g.counter -%= 1;
        if (g.counter < 0) return 0;
        var count: c_int = undefined;
        if (g.counter == 0) {
            count = @as(c_int, @bitCast(section_total)) -% g.total;
            if (count == 0) return 0;
            clut_hi.* = @as(c_uint, @bitCast(clut)) << 16;
            return count;
        }
        count = if (alternate_signed) @as(c_int, @as(i16, @bitCast(g.counts[0]))) else @as(c_int, g.counts[0]);
        g.counts += 1;
        g.total +%= count;
        if (count == 0) continue;
        // 1 <= counter <= 8 here
        const shift: u5 = @intCast(g.counter - 1);
        const value: c_int = clut +% (if ((mask >> shift) & 1 != 0) @as(c_int, 0x480) else 0);
        clut_hi.* = @as(c_uint, @bitCast(value)) << 16;
        return count;
    }
}

fn mtc2(value: c_uint, reg: c_int) void {
    c.MTC2(value, reg);
}

/// orig: nclip3 (pc/src/weapon_packets.c)
fn nclip3(a: c_uint, b: c_uint, cc: c_uint) c_int {
    mtc2(a, 12);
    mtc2(b, 13);
    mtc2(cc, 14);
    _ = c.doCOP2(0x01400006); // gte_nclip
    return @bitCast(c.MFC2(24));
}

/// orig: avsz3 (pc/src/weapon_packets.c)
fn avsz3(a: c_uint, b: c_uint, cc: c_uint) c_uint {
    mtc2(a, 17);
    mtc2(b, 18);
    mtc2(cc, 19);
    _ = c.doCOP2(0x0158002D); // gte_avsz3
    return c.MFC2(7);
}

/// `(short)w` and `(short)(w >> 16)` as ints.
fn lowShort(w: c_uint) c_int {
    return psx.conv(i16, w);
}

fn highShort(w: c_uint) c_int {
    return psx.conv(i16, w >> 16);
}

/// orig: split_diagonal (pc/src/weapon_packets.c)
/// Whether the quad's far diagonal is the split (the sign of the GTE OP determinant); 32-bit wrapping.
fn splitDiagonal(xy: *const [4]c_uint, z: *const [4]c_uint) bool {
    const x0 = lowShort(xy[0]);
    const y0 = highShort(xy[0]);
    c.CTC2(@bitCast(lowShort(xy[2]) -% x0), 0);
    c.CTC2(@bitCast(highShort(xy[2]) -% y0), 2);
    c.CTC2(z[2] -% z[0], 4);
    mtc2(@bitCast(lowShort(xy[1]) -% x0), 9);
    mtc2(@bitCast(highShort(xy[1]) -% y0), 10);
    mtc2(z[1] -% z[0], 11);
    _ = c.doCOP2(0x0170000C); // gte_op0
    const dx: c_uint = @bitCast(lowShort(xy[3]) -% x0);
    const dy: c_uint = @bitCast(highShort(xy[3]) -% y0);
    const dot: c_uint = (dx *% c.MFC2(25)) +% (dy *% c.MFC2(26)) +% ((z[3] -% z[0]) *% c.MFC2(27));
    return @as(c_int, @bitCast(0 -% dot)) > 0;
}

/// orig: blend_triangle (pc/src/weapon_packets.c)
fn blendTriangle(s: *BlendState, code: c_uint, xy: *const [4]c_uint, uv0: c_uint, uv1: c_uint, uv2: c_uint, depth: c_uint, bucket_bias: c_int, tpage_bits: c_uint) void {
    const p = s.packet;
    p[1] = code | s.color;
    p[2] = xy[0];
    p[3] = (uv0 & 0xffff) | s.clut_hi;
    if (depth >= 0x7c0) return;
    p[4] = xy[1];
    p[5] = (uv1 & 0xffff) | s.tpage_hi | tpage_bits;
    p[6] = xy[2];
    p[7] = uv2;
    linkPacket(s, p, @as(c_int, @bitCast(depth)) +% bucket_bias, 7);
    s.packet = p + 8;
}

/// orig: blend_quad (pc/src/weapon_packets.c)
fn blendQuad(s: *BlendState, code: c_uint, xy: *const [4]c_uint, uv01: c_uint, uv23: c_uint, depth: c_uint, bucket_bias: c_int, tpage_bits: c_uint) void {
    if (depth >= 0x7c0) return;
    const p = s.packet;
    p[1] = code | s.color;
    p[2] = xy[0];
    p[3] = (uv01 & 0xffff) | s.clut_hi;
    p[4] = xy[1];
    p[5] = (uv01 >> 16) | s.tpage_hi | tpage_bits;
    p[6] = xy[2];
    p[7] = uv23;
    p[8] = xy[3];
    p[9] = uv23 >> 16;
    linkPacket(s, p, @as(c_int, @bitCast(depth)) +% bucket_bias, 9);
    s.packet = p + 10;
}

/// orig: blend_split (pc/src/weapon_packets.c)
/// A double-sided split quad: two triangles along the diagonal splitDiagonal picks.
fn blendSplit(s: *BlendState, xy: *const [4]c_uint, z: *const [4]c_uint, uv01: c_uint, uv23: c_uint) void {
    const code: c_uint = if (s.blend != 0) 0x26000000 else 0x24000000;
    var tri: [4]c_uint = undefined; // the fourth word is never read
    if (!splitDiagonal(xy, z)) {
        tri[0] = xy[0];
        tri[1] = xy[1];
        tri[2] = xy[3];
        if (nclip3(tri[0], tri[1], tri[2]) > 0)
            blendTriangle(s, code, &tri, uv01, uv01 >> 16, uv23 >> 16, avsz3(z[0], z[1], z[3]), -1, s.blend);
        tri[0] = xy[0];
        tri[1] = xy[3];
        tri[2] = xy[2];
        const p = s.packet;
        p[1] = code | s.color;
        p[2] = xy[0];
        if (nclip3(tri[0], tri[1], tri[2]) <= 0) {
            s.packet = p + 8; // reserved without a link, as in the original
            return;
        }
        blendTriangle(s, code, &tri, uv01, uv23 >> 16, uv23, avsz3(z[0], z[2], z[3]), -1, s.blend);
        return;
    }
    tri[0] = xy[0];
    tri[1] = xy[1];
    tri[2] = xy[2];
    if (nclip3(tri[0], tri[1], tri[2]) > 0)
        blendTriangle(s, code, &tri, uv01, uv01 >> 16, uv23, avsz3(z[0], z[1], z[2]), -1, s.blend);
    tri[0] = xy[1];
    tri[1] = xy[3];
    tri[2] = xy[2];
    if (nclip3(tri[0], tri[1], tri[2]) > 0)
        blendTriangle(s, code, &tri, uv01 >> 16, uv23 >> 16, uv23, avsz3(z[3], z[1], z[2]), -1, s.blend);
}

fn word(base: anytype, offset: usize) c_uint {
    return psx.byteAt(c_uint, base, @bitCast(offset)).*;
}

/// orig: func_800B516C (pc/src/weapon_packets.c)
/// Builds the blended (translucent) packets of an object from the projected
/// vertices (screen words, depths 0xC00 bytes on) and the model's polygon
/// records at model +0x3C: triangles and quads of section 0 and 1, then the
/// mixed records, colour from `color`'s word, the texture and CLUT from
/// scratchpad 0x390/0x392; moves the packet cursor (word 0) on and hands the
/// new packets to the native renderer.
pub fn buildBlendedWeaponPackets(projected: [*]u8, object: [*]u8, color: [*]u8, model: [*]u8) callconv(.c) void {
    const scratch: *u32 = @ptrFromInt(0x1f800000);
    const texture = @as(*c_uint, @ptrFromInt(0x1f800390)).*;
    const clut: c_int = @as(*i16, @ptrFromInt(0x1f800392)).*;
    var s = BlendState{
        .packet = @ptrFromInt(@as(usize, scratch.*)),
        .ot = @ptrFromInt(@as(usize, @as(*u32, @ptrFromInt(0x1f800004)).*)),
        .color = word(color, 0) & 0xffffff,
        .tpage_hi = texture << 16,
        .blend = 0,
        .clut_hi = 0,
    };
    const start = s.packet;
    const mode: c_uint = object[9] & 0xf;
    s.blend = if (mode == 0) 0 else if (mode < 7) 0x400000 else 0x200000;
    const mask: c_uint = object[0x64];
    var record: [*]u8 = @ptrFromInt(@as(usize, psx.byteAt(u32, model, 0x3c).*));
    var counts: [*]align(1) const u16 = @ptrCast(model);
    const totals: [*]align(1) u16 = @ptrCast(object + 2);
    // Triangles (16-byte records) and quads (20-byte records).
    var section: usize = 0;
    while (section < 2) : (section += 1) {
        const quad = section == 1;
        var g = GroupWalk{ .counter = 9, .total = 0, .done = 0, .counts = counts };
        if (totals[section] != 0) {
            while (true) {
                var count = nextGroup(&g, totals[section], mask, clut, true, &s.clut_hi);
                if (count == 0) break;
                while (count != 0) : ({
                    count -%= 1;
                    record += if (quad) 0x14 else 0x10;
                }) {
                    const o0: usize = word(record, 4) & 0xffff;
                    const o1: usize = word(record, 4) >> 16;
                    const o2: usize = word(record, 8) & 0xffff;
                    var xy = [4]c_uint{ word(projected, o0), word(projected, o1), word(projected, o2), 0 };
                    if (nclip3(xy[0], xy[1], xy[2]) <= 0) continue;
                    if (!quad) {
                        const z0 = word(projected, 0xc00 + o0);
                        const z1 = word(projected, 0xc00 + o1);
                        const z2 = word(projected, 0xc00 + o2);
                        blendTriangle(&s, if (s.blend != 0) 0x26000000 else 0x24000000, &xy, word(record, 12), word(record, 12) >> 16, word(record, 8) >> 16, avsz3(z0, z1, z2), -1, s.blend);
                        continue;
                    }
                    const o3: usize = word(record, 8) >> 16;
                    xy[3] = word(projected, o3);
                    const depth = (word(projected, 0xc00 + o0) +% word(projected, 0xc00 + o1) +% word(projected, 0xc00 + o2) +% word(projected, 0xc00 + o3)) >> 4;
                    blendQuad(&s, if (s.blend != 0) 0x2e000000 else 0x2c000000, &xy, word(record, 12), word(record, 16), depth, -1, s.blend);
                }
            }
            counts = g.counts;
        } else {
            counts += 8;
        }
    }
    // Mixed records: header (code, size, flags << 16), then as above.
    var g = GroupWalk{ .counter = 9, .total = 0, .done = 0, .counts = counts };
    while (totals[2] != 0) {
        var count = nextGroup(&g, totals[2], object[0x64], @as(c_int, psx.conv(u16, clut)), false, &s.clut_hi);
        if (count == 0) break;
        while (count != 0) : (count -%= 1) {
            var header = word(record, 0);
            const flags = header >> 16;
            const o0: usize = word(record, 4) & 0xffff;
            const o1: usize = word(record, 4) >> 16;
            const o2: usize = word(record, 8) & 0xffff;
            const o3: usize = word(record, 8) >> 16;
            const xy = [4]c_uint{ word(projected, o0), word(projected, o1), word(projected, o2), word(projected, o3) };
            if (flags & 1 == 0) {
                if (nclip3(xy[0], xy[1], xy[2]) <= 0) {
                    record += (header >> 8) & 0xff;
                    continue;
                }
            } else if (flags & 2 != 0) {
                const z = [4]c_uint{ word(projected, 0xc00 + o0), word(projected, 0xc00 + o1), word(projected, 0xc00 + o2), word(projected, 0xc00 + o3) };
                blendSplit(&s, &xy, &z, word(record, 12), word(record, 16));
                record += 0x14;
                continue;
            }
            if (s.blend != 0) header = (header & 0xff0fffff) | 0x100000 | s.blend;
            const code_bit = ((header >> 20) & 1) << 25;
            const tpage_bits = ((header >> 16) & 0x60) << 16;
            const bias: c_int = @as(c_int, @intCast((header >> 18) & 3)) *% 4 -% 5;
            if (header & 0xff == 0x2c) {
                const depth = (word(projected, 0xc00 + o0) +% word(projected, 0xc00 + o1) +% word(projected, 0xc00 + o2) +% word(projected, 0xc00 + o3)) >> 4;
                blendQuad(&s, 0x2c000000 | code_bit, &xy, word(record, 12), word(record, 16), depth, bias, tpage_bits);
                record += 0x14;
            } else {
                blendTriangle(&s, 0x24000000 | code_bit, &xy, word(record, 12), word(record, 12) >> 16, @truncate(o3), avsz3(word(projected, 0xc00 + o0), word(projected, 0xc00 + o1), word(projected, 0xc00 + o2)), bias, tpage_bits);
                record += 0x10;
            }
        }
    }
    scratch.* = psx.addr(s.packet);
    if (psx.weak(NativeDraw, "pc_object_native_draw")) |draw| draw(2, projected, object, model, color, start);
}
