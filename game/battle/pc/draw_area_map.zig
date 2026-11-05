//! Area map drawing (D2: 2EA3C.s; oracle pc/src/room_geometry.c).
const c = @import("c_room_geometry");
const v = @import("views_room_geometry");
const psx = @import("common").psx;
const gte = @import("common").gte;

/// `p + n` for a word pointer with a C `unsigned`/`int` count (the address wraps like C's).
fn advance(p: [*]c_uint, n: usize) [*]c_uint {
    return @ptrFromInt(@intFromPtr(p) +% n *% 4);
}

/// orig: chain (pc/src/room_geometry.c)
/// Links `packet` (length `words`) at the head of the OT chain `head`.
fn chain(packet: [*]c_uint, head: *c_uint, words: c_uint) [*]c_uint {
    packet[0] = (head.* & 0xffffff) | (words << 24);
    head.* = (head.* & 0xff000000) | (psx.addr(packet) & 0xffffff);
    return packet;
}

/// orig: func_80098F8C (pc/src/room_geometry.c)
/// Draws the area map: `rooms` is a count then 12-byte entries (visible flag,
/// geometry); the current room gets filled shapes, edge lines and markers,
/// every room its outline lines.
pub fn drawAreaMap(rooms: [*]c_uint) callconv(.c) void {
    const table = rooms;
    const count: c_uint = table[0];
    const ot: [*]c_uint = @ptrFromInt(@as(usize, @as(*const c_uint, @ptrFromInt(0x1f800004)).*));
    var packet: [*]c_uint = @ptrFromInt(@as(usize, @as(*const c_uint, @ptrFromInt(0x1f800000)).*));
    const xy: [*]c_uint = @ptrFromInt(0x1f800100);
    var shapes: c_uint = ot[5];
    var markers: c_uint = ot[4];
    const blink: c_uint = v.g.D_800F1CDC().*;
    c.CTC2(0, 5);
    c.CTC2(0, 6);
    c.CTC2(0x100, 7);
    var room: c_uint = 0;
    while (room < count) : (room +%= 1) {
        const entry = advance(table, 1 +% @as(usize, room) *% 3);
        var data: [*]c_uint = @ptrFromInt(@as(usize, entry[1]));
        if (entry[0] == 0 or data[0] == 0) continue;
        const vertices: c_int = @bitCast(data[0]);
        data += 1;
        var out = xy;
        var left = vertices;
        while (left > 0) : ({
            left -%= 3;
            data += 6;
            out += 3;
        }) {
            c.MTC2(data[0], 0);
            c.MTC2(data[1], 1);
            c.MTC2(data[2], 2);
            c.MTC2(data[3], 3);
            c.MTC2(data[4], 4);
            c.MTC2(data[5], 5);
            gte.rtpt();
            out[0] = c.MFC2(12);
            out[1] = c.MFC2(13);
            out[2] = c.MFC2(14);
        }
        const base: [*]c_uint = @ptrFromInt(@as(usize, entry[1]));
        data = @ptrFromInt(@intFromPtr(base) +% @as(usize, @bitCast(@as(isize, 1 +% vertices *% 2))) *% 4);
        var n: c_uint = data[0];
        data += 1;
        if (room != 0) {
            data = advance(data, n);
        } else while (n != 0) : ({
            n -%= 1;
            data += 1;
        }) {
            const p = chain(packet, &shapes, 4);
            p[1] = 0x223f7f00;
            p[2] = xy[data[0] & 0xff];
            p[3] = xy[(data[0] >> 8) & 0xff];
            p[4] = xy[(data[0] >> 16) & 0xff];
            packet += 5;
        }
        n = data[0];
        data += 1;
        if (room != 0) {
            data = advance(data, n);
        } else while (n != 0) : ({
            n -%= 1;
            data += 1;
        }) {
            const p = chain(packet, &shapes, 5);
            p[1] = 0x2a3f7f00;
            p[2] = xy[data[0] & 0xff];
            p[3] = xy[(data[0] >> 8) & 0xff];
            p[4] = xy[(data[0] >> 24) & 0xff];
            p[5] = xy[(data[0] >> 16) & 0xff];
            packet += 6;
        }
        n = data[0];
        data += 1;
        while (n != 0) : ({
            n -%= 1;
            data += 1;
        }) {
            const p = chain(packet, &shapes, 3);
            p[1] = 0x42ff7f00;
            p[2] = xy[data[0] & 0xff];
            p[3] = xy[(data[0] >> 8) & 0xff];
            packet += 4;
        }
        if (room != 0) continue;
        n = data[0];
        data += 1;
        while (n != 0) : ({
            n -%= 1;
            data += 1;
        }) {
            const p = chain(packet, &shapes, 3);
            p[1] = 0x422f5f00;
            p[2] = xy[data[0] & 0xff];
            p[3] = xy[(data[0] >> 8) & 0xff];
            packet += 4;
        }
        n = data[0];
        data += 1;
        while (n != 0) : ({
            n -%= 1;
            data += 1;
        }) {
            const word = data[0];
            const flags = word >> 16;
            const state = (word >> 24) & 0xff;
            const at = xy[word & 0xff];
            const x: c_int = @intCast(at & 0xffff);
            const y: c_int = @as(c_int, @bitCast(at)) >> 16;
            if (state != 0 and v.g.vs_main_stateFlags()[state + 0x33f] == 0) { // (signed char) == 0
                const p = chain(packet, &markers, 3);
                p[1] = 0x74808080;
                p[2] = @as(c_uint, @bitCast((y -% 4) << 16)) | @as(c_uint, @bitCast((x -% 3) & 0xffff));
                p[3] = 0x37f898d8;
                packet += 4;
                continue;
            }
            var colour: c_uint = undefined;
            if (flags & 0x10 != 0) {
                if (blink & 0x20 == 0) continue;
                colour = 0x607f00ff;
            } else if (flags & 4 != 0) {
                colour = 0x60ff7f00;
            } else if (flags & 0x1f == 0) {
                colour = 0x607f7f7f;
            } else continue;
            var p = chain(packet, &markers, 3);
            p[1] = colour;
            p[2] = @as(c_uint, @bitCast((y -% 1) << 16)) | @as(c_uint, @bitCast(x));
            p[3] = 0x00030001;
            packet += 4;
            p = chain(packet, &markers, 3);
            p[1] = colour;
            p[2] = @as(c_uint, @bitCast(y << 16)) | @as(c_uint, @bitCast((x -% 1) & 0xffff));
            p[3] = 0x00010003;
            packet += 4;
        }
    }
    var p = chain(packet, &shapes, 2);
    p[1] = 0xe100020c;
    p[2] = 0;
    packet += 4;
    p = chain(packet, &markers, 2);
    p[1] = 0xe100020c;
    p[2] = 0;
    packet += 4;
    ot[4] = markers;
    ot[5] = shapes;
    @as(*c_uint, @ptrFromInt(0x1f800000)).* = psx.addr(packet);
}
