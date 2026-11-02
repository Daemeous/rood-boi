//! zig_diff spec: _getDoorId -> get_door_id.getDoorId (146C.c).
//! Inputs: no scene, or an arena scene with small lists (room counts that
//! wrap included) and 0..8 doors (or a negative count); door below 0, at
//! 0, inside, at and past the count; the id bytes are random (high bit
//! too: the C reads them as unsigned char).
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").getDoorId));

pub const classes = [_][:0]const u8{ "no-scene", "negative", "past", "door" };

const words = 64;

/// The room's scene (section F) as the C walks it: the room count and its
/// rooms (3 ints each), then five counted lists (the first of pairs), then
/// the door list (a count and one word per door). The C multiplies the room
/// count by 3 in 32 bits: two counts wrap to a small offset.
fn scene(ctx: *zd.Ctx) struct { *v.vs_battle_scene, [*]i32, c_int } {
    const a: [*]i32 = @ptrCast(@alignCast(ctx.allocBytes(words * 4, 16)));
    for (0..words) |k| a[k] = ctx.int(i32);
    const rooms = ctx.edge(i32, &.{ 0, 1, 2, 3, 0x5555_5556, 0x5555_5557 });
    a[0] = rooms;
    var at: usize = @intCast(rooms *% 3 +% 1);
    const first = ctx.range(i32, 0, 3);
    a[at] = first;
    at += @intCast(first * 2 + 1);
    for (0..4) |_| {
        const n = ctx.range(i32, 0, 3);
        a[at] = n;
        at += @intCast(n + 1);
    }
    const count = ctx.edge(i32, &.{ 0, 1, 2, 5, 8, -1, -0x8000_0000 });
    a[at] = count;
    return .{ @ptrCast(a), a + at + 1, count };
}

var doors: c_int = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const room = v.g.vs_battle_roomData();
    if (ctx.chance(6)) {
        room.sectionF.raw = 0;
        return .{ctx.int(c_int)};
    }
    const s = scene(ctx);
    psx.put(&room.sectionF, s[0]);
    doors = s[2];
    const door = ctx.pick(c_int, &.{ -1, 0, 1, doors -% 1, doors, doors +% 1, -0x8000_0000 });
    return .{door};
}

pub fn classify(args: Args) usize {
    if (v.g.vs_battle_roomData().sectionF.raw == 0) return 0;
    if (args[0] < 0) return 1;
    return if (args[0] >= doors) 2 else 3;
}
