//! Semi-transparent per-vertex-colour actor packets (D2: 4B16C.s; oracle pc/src/actor_packets.c).
const c = @import("c_actor_packets");
const gte = @import("common").gte;
const psx = @import("common").psx;

const NativeDraw = *const fn (c_int, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque, ?*const anyopaque, ?[*]c_uint) callconv(.c) void;

/// orig: actor_vertex (pc/src/actor_packets.c)
/// A vertex offset in bytes as an index into the projected arrays.
fn actorVertex(offset: c_uint) c_uint {
    if (offset & 3 != 0 or offset >= 0xc00) c.pc_unimplemented("actor polygon vertex offset");
    return offset >> 2;
}

/// orig: actor_nclip (pc/src/actor_packets.c)
/// GTE NCLIP of three screen words.
fn actorNclip(a: c_uint, b: c_uint, third: c_uint) c_int {
    c.MTC2(a, 12);
    c.MTC2(b, 13);
    c.MTC2(third, 14);
    gte.nclip();
    return @bitCast(c.MFC2(24));
}

/// orig: actor_avsz3 (pc/src/actor_packets.c)
/// GTE AVSZ3 of three depth words.
fn actorAvsz3(a: c_uint, b: c_uint, third: c_uint) c_uint {
    c.MTC2(a, 17);
    c.MTC2(b, 18);
    c.MTC2(third, 19);
    gte.avsz3();
    return c.MFC2(7);
}

/// orig: actor_link (pc/src/actor_packets.c)
/// Links `packet` into ordering table bucket `bucket`, `words` words long.
fn actorLink(ot: [*]c_uint, bucket: c_int, packet: [*]c_uint, words: c_uint) void {
    const head = psx.at(ot, bucket);
    packet[0] = (head.* & 0xffffff) | (words << 24);
    head.* = (head.* & 0xff000000) | (psx.addr(packet) & 0xffffff);
}

/// orig: actor_blend_group (pc/src/actor_packets.c)
/// The next non-empty material group of a section (its count, and its CLUT
/// word in `clut_hi`), or 0 when the section is finished. The counter and the
/// running total are the scratchpad halfwords 0x39C and 0x39E.
fn actorBlendGroup(counts: *[*]const u16, section_total: c_uint, signed_counts: bool, mask: c_uint, clut_hi: *c_uint) c_int {
    const state: [*]u16 = @ptrFromInt(0x1f80039c);
    while (true) {
        const counter: c_int = @as(c_int, state[0]) -% 1;
        state[0] = psx.conv(u16, counter);
        if (counter < 0) return 0;
        var clut: c_uint = @as(*u16, @ptrFromInt(0x1f800392)).*;
        var count: c_int = undefined;
        if (counter == 0) {
            count = @as(c_int, @bitCast(section_total)) -% state[1];
            if (count == 0) return 0;
        } else {
            count = if (signed_counts) @as(c_int, @as(*const i16, @ptrCast(counts.*)).*) else counts.*[0];
            counts.* += 1;
            state[1] = psx.conv(u16, @as(c_int, state[1]) +% count);
            if (count == 0) continue;
            // The shift count is at most 8 for the walk state the function sets up.
            if ((mask >> @truncate(@as(c_uint, @bitCast(counter -% 1)))) & 1 != 0) clut +%= 0x480;
        }
        if (count < 0) c.pc_unimplemented("negative actor material group count");
        clut_hi.* = clut << 16;
        return count;
    }
}

/// orig: func_800B5C30 (pc/src/actor_packets.c)
/// Semi-transparent variant of buildActorPackets: records are walked in
/// material groups (8 counts at the model, then the remainder; CLUT + 0x480
/// for groups flagged in object byte 0x64); when object byte 9 selects a blend
/// mode every vertex takes the object colour and the packets become
/// semi-transparent GT3/GT4. Kept from the original: an empty section does not
/// skip its 8 counts, a depth-rejected triangle still writes part of its
/// packet, and the walk state is left in 0x1F80039C..A3.
pub fn buildBlendedActorPackets(projected: [*]c_uint, object: [*]u8, color: [*]const c_uint, model: [*]u8) callconv(.c) void {
    const xy = projected;
    const z = projected + 0x300;
    const totals: [*]const u16 = @ptrCast(@alignCast(object + 2));
    var counts: [*]const u16 = @ptrCast(@alignCast(model));
    var r: [*c]const c_uint = @ptrFromInt(@as(usize, psx.byteAt(u32, model, 0x3c).*));
    const scratch: *u32 = @ptrFromInt(0x1f800000);
    var packet: [*]c_uint = @ptrFromInt(@as(usize, scratch.*));
    const start: ?[*]c_uint = packet;
    const ot: [*]c_uint = @ptrFromInt(@as(usize, @as(*u32, @ptrFromInt(0x1f800004)).*));
    const tpage_hi: c_uint = @as(*c_uint, @ptrFromInt(0x1f800390)).* << 16;
    const tint = color[0] & 0xffffff;
    const mode: c_uint = object[9] & 0xf;
    const blend: c_uint = if (mode == 0) 0 else if (mode < 7) 0x400000 else 0x200000;
    const mask: c_uint = object[0x64];
    var clut_hi: c_uint = 0;
    var count: c_int = undefined;
    const walk: *u32 = @ptrFromInt(0x1f80039c);

    // Triangles, 24-byte records: o0|o1, o2|uv2, c0, c1, c2, uv0|uv1.
    walk.* = 9;
    if (totals[0] != 0) {
        while (true) {
            count = actorBlendGroup(&counts, totals[0], true, mask, &clut_hi);
            if (count == 0) break;
            while (count != 0) : ({
                count -%= 1;
                r += 6;
            }) {
                const n0 = actorVertex(r[0] & 0xffff);
                const n1 = actorVertex(r[0] >> 16);
                const n2 = actorVertex(r[1] & 0xffff);
                if (actorNclip(xy[n0], xy[n1], xy[n2]) <= 0) continue;
                var c0 = r[2];
                var c1 = r[3];
                var c2 = r[4];
                if (blend != 0) {
                    c0 = tint | 0x36000000;
                    c1 = tint;
                    c2 = tint;
                }
                const depth = actorAvsz3(z[n0], z[n1], z[n2]);
                // Written before the depth test, so they stay on rejection.
                packet[1] = c0;
                packet[2] = xy[n0];
                packet[3] = (r[5] & 0xffff) | clut_hi;
                packet[4] = c1;
                packet[5] = xy[n1];
                if (depth >= 0x7c0) continue;
                packet[6] = (r[5] >> 16) | tpage_hi | blend;
                packet[7] = c2;
                packet[8] = xy[n2];
                packet[9] = r[1] >> 16;
                actorLink(ot, @as(c_int, @bitCast(depth)) -% 1, packet, 9);
                packet += 10;
            }
        }
    }
    // Quads, 32-byte records: o0|o1, o2|o3, c0..c3, uv0|uv1, uv2|uv3.
    walk.* = 9;
    if (totals[1] != 0) {
        while (true) {
            count = actorBlendGroup(&counts, totals[1], true, mask, &clut_hi);
            if (count == 0) break;
            while (count != 0) : ({
                count -%= 1;
                r += 8;
            }) {
                const n0 = actorVertex(r[0] & 0xffff);
                const n1 = actorVertex(r[0] >> 16);
                const n2 = actorVertex(r[1] & 0xffff);
                const n3 = actorVertex(r[1] >> 16);
                if (actorNclip(xy[n0], xy[n1], xy[n2]) <= 0) continue;
                const depth = (z[n0] +% z[n1] +% z[n2] +% z[n3]) >> 4;
                if (depth >= 0x7c0) continue;
                var c0 = r[2];
                var c1 = r[3];
                var c2 = r[4];
                var c3 = r[5];
                if (blend != 0) {
                    c0 = tint | 0x3e000000;
                    c1 = tint;
                    c2 = tint;
                    c3 = tint;
                }
                packet[1] = c0;
                packet[2] = xy[n0];
                packet[3] = (r[6] & 0xffff) | clut_hi;
                packet[4] = c1;
                packet[5] = xy[n1];
                packet[6] = (r[6] >> 16) | tpage_hi | blend;
                packet[7] = c2;
                packet[8] = xy[n2];
                packet[9] = r[7];
                packet[10] = c3;
                packet[11] = xy[n3];
                packet[12] = r[7] >> 16;
                actorLink(ot, @as(c_int, @bitCast(depth)) -% 1, packet, 12);
                packet += 13;
            }
        }
    }
    // Mixed records: triangle or quad by the c0 code (0x3c), flags and bias in
    // the c2 top byte, stride in the c1 top byte when culled.
    walk.* = 9;
    @as(*u32, @ptrFromInt(0x1f8003a0)).* = psx.addr(object);
    if (totals[2] != 0) {
        while (true) {
            count = actorBlendGroup(&counts, totals[2], false, mask, &clut_hi);
            if (count == 0) break;
            var flags: c_uint = r[4] >> 24;
            while (count != 0) : (count -%= 1) {
                const n0 = actorVertex(r[0] & 0xffff);
                const n1 = actorVertex(r[0] >> 16);
                const n2 = actorVertex(r[1] & 0xffff);
                if (flags & 1 == 0 and actorNclip(xy[n0], xy[n1], xy[n2]) <= 0) {
                    const stride = r[3] >> 24;
                    if (stride < 24 or stride & 3 != 0) c.pc_unimplemented("actor polygon stride");
                    r += stride / 4;
                    // As in buildActorPackets, the culled branch reloads flags
                    // from word 0 of the next record.
                    if (count > 1) flags = r[0] >> 24;
                    continue;
                }
                var c0 = r[2];
                var c1 = r[3];
                var c2 = r[4];
                if (c0 >> 24 == 0x3c) {
                    const n3 = actorVertex(r[1] >> 16);
                    const depth = (z[n0] +% z[n1] +% z[n2] +% z[n3]) >> 4;
                    if (depth < 0x7c0) {
                        var c3 = r[5];
                        if (blend != 0) {
                            c0 = tint | 0x3e000000;
                            c1 = tint;
                            c3 = tint;
                            c2 = (c2 & 0xff000000) | tint;
                        }
                        packet[1] = c0;
                        packet[2] = xy[n0];
                        packet[3] = (r[6] & 0xffff) | clut_hi;
                        packet[4] = c1;
                        packet[5] = xy[n1];
                        packet[6] = (r[6] >> 16) | tpage_hi | blend;
                        packet[7] = c2;
                        packet[8] = xy[n2];
                        packet[9] = r[7];
                        packet[10] = c3;
                        packet[11] = xy[n3];
                        packet[12] = r[7] >> 16;
                        actorLink(ot, @as(c_int, @bitCast(depth +% ((c2 >> 26) & 3) *% 4)) -% 5, packet, 12);
                        packet += 13;
                    }
                    r += 8;
                } else {
                    if (blend != 0) {
                        c0 = tint | 0x36000000;
                        c1 = tint;
                        c2 = (c2 & 0xff000000) | tint;
                    }
                    const depth = actorAvsz3(z[n0], z[n1], z[n2]);
                    // Written before the depth test, so they stay on rejection.
                    packet[1] = c0;
                    packet[2] = xy[n0];
                    if (depth < 0x7c0) {
                        packet[3] = (r[5] & 0xffff) | clut_hi;
                        packet[4] = c1;
                        packet[5] = xy[n1];
                        packet[6] = (r[5] >> 16) | tpage_hi | blend;
                        packet[7] = c2;
                        packet[8] = xy[n2];
                        packet[9] = r[1] >> 16;
                        actorLink(ot, @as(c_int, @bitCast(depth +% ((c2 >> 26) & 3) *% 4)) -% 5, packet, 9);
                        packet += 10;
                    }
                    r += 6;
                }
                if (count > 1) flags = r[4] >> 24;
            }
        }
    }
    scratch.* = psx.addr(packet);
    if (psx.weak(NativeDraw, "pc_object_native_draw")) |draw| draw(3, projected, object, model, color, start);
}
