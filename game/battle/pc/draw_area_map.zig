//! Area map drawing (D2: 2EA3C.s; oracle pc/src/room_geometry.c).
const c = @import("c_room_geometry");
const v = @import("views_room_geometry");
const common = @import("common");
const pad = @import("common").pad;
const psx = common.psx;
const gte = @import("common").gte;
const sprite_item = @import("common").sprite_item;

/// `p + n` for a word pointer with a C `unsigned`/`int` count (the address wraps like C's).
fn advance(p: [*]c_uint, n: usize) [*]c_uint {
    return @ptrFromInt(@intFromPtr(p) +% n *% 4);
}

fn bits(value: c_uint) c_int {
    return @bitCast(value);
}

/// orig: vs_field_drawMinimapScene (pc/src/room_geometry.c)
/// Draws the area map: `rooms` is a count then 12-byte entries (visible flag,
/// geometry); the current room gets filled shapes, edge lines and markers,
/// every room its outline lines. The shapes are draw items placed at the table's slot 5 and the markers at slot 4, each linked at
/// the slot's head as the packets were (the last made draws first).
pub fn drawAreaMap(rooms: [*]c_uint, camera: *const gte.View) callconv(.c) void {
    const table = rooms;
    const count: c_uint = table[0];
    const scratch = common.scratch.get();
    const ot: [*]c_uint = @ptrCast(@alignCast(scratch.unk4.?));
    const xy: [*]c_uint = @ptrFromInt(pad.addr(0x100));
    const shapes: *c_uint = &ot[5];
    const markers: *c_uint = &ot[4];
    const blink: c_uint = v.g.vs_field_roomAnimationTicks().*;
    var view = camera.*;
    view.t = .{ 0, 0, 0x100 };
    var room: c_uint = 0;
    while (room < count) : (room +%= 1) {
        const entry = advance(table, 1 +% @as(usize, room) *% 3);
        var data: [*]c_uint = @ptrFromInt(@intFromPtr(table) +% entry[1]); // the entry holds the room's offset from the table
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
            // RTPT: three vertices, each an x/y word and a z word.
            for (0..3) |k| {
                const point = [3]i16{ psx.conv(i16, data[2 * k]), psx.conv(i16, data[2 * k] >> 16), psx.conv(i16, data[2 * k + 1]) };
                out[k] = @bitCast(gte.project(&view, &point).sxy);
            }
        }
        const base: [*]c_uint = @ptrFromInt(@intFromPtr(table) +% entry[1]);
        data = @ptrFromInt(@intFromPtr(base) +% @as(usize, @bitCast(@as(isize, 1 +% vertices *% 2))) *% 4);
        var n: c_uint = data[0];
        data += 1;
        if (room != 0) {
            data = advance(data, n);
        } else while (n != 0) : ({
            n -%= 1;
            data += 1;
        }) {
            sprite_item.flat(shapes, 3, true, 0x3f7f00, &[3]c_int{ bits(xy[data[0] & 0xff]), bits(xy[(data[0] >> 8) & 0xff]), bits(xy[(data[0] >> 16) & 0xff]) });
        }
        n = data[0];
        data += 1;
        if (room != 0) {
            data = advance(data, n);
        } else while (n != 0) : ({
            n -%= 1;
            data += 1;
        }) {
            sprite_item.flat(shapes, 4, true, 0x3f7f00, &[4]c_int{ bits(xy[data[0] & 0xff]), bits(xy[(data[0] >> 8) & 0xff]), bits(xy[(data[0] >> 24) & 0xff]), bits(xy[(data[0] >> 16) & 0xff]) });
        }
        n = data[0];
        data += 1;
        while (n != 0) : ({
            n -%= 1;
            data += 1;
        }) {
            sprite_item.lineFlat(shapes, true, 0xff7f00, bits(xy[data[0] & 0xff]), bits(xy[(data[0] >> 8) & 0xff]));
        }
        if (room != 0) continue;
        n = data[0];
        data += 1;
        while (n != 0) : ({
            n -%= 1;
            data += 1;
        }) {
            sprite_item.lineFlat(shapes, true, 0x2f5f00, bits(xy[data[0] & 0xff]), bits(xy[(data[0] >> 8) & 0xff]));
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
                // (an 8 x 8 textured rectangle, colour 0x808080, in the GPU's draw mode)
                _ = sprite_item.sprite(markers, 128, @bitCast(@as(c_uint, @bitCast((y -% 4) << 16)) | @as(c_uint, @bitCast((x -% 3) & 0xffff))), 8 | (8 << 16), 0, 0x37f898d8);
                continue;
            }
            var colour: c_int = undefined; // (0xBBGGRR)
            if (flags & 0x10 != 0) {
                if (blink & 0x20 == 0) continue;
                colour = 0x7f00ff;
            } else if (flags & 4 != 0) {
                colour = 0xff7f00;
            } else if (flags & 0x1f == 0) {
                colour = 0x7f7f7f;
            } else continue;
            // (a plus: a bar one wide and three high, and one three wide and one high)
            sprite_item.tileFlat(markers, false, colour, @bitCast(@as(c_uint, @bitCast((y -% 1) << 16)) | @as(c_uint, @bitCast(x))), 0x00030001);
            sprite_item.tileFlat(markers, false, colour, @bitCast(@as(c_uint, @bitCast(y << 16)) | @as(c_uint, @bitCast((x -% 1) & 0xffff))), 0x00010003);
        }
    }
    sprite_item.drawMode(shapes, 0x20c);
    sprite_item.drawMode(markers, 0x20c);
}
