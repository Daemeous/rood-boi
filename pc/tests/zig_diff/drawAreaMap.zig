//! zig_diff spec: func_80098F8C -> draw_area_map.drawAreaMap (D2: pc/src/room_geometry.c).
//! Inputs: a room table in the arena (a count, then 12-byte entries: a
//! visible flag and the room's geometry address), each room's geometry (a
//! vertex count that is a multiple of 3 and may be 0, 6 words per three
//! vertices, then the triangle, quad, line, edge-line and marker lists, each
//! a count and packed words whose index bytes are anything, so the V()
//! lookups read projected points and stale scratchpad alike), the packet
//! buffer and ordering table at scratchpad words 0 and 1 (OT[4] and OT[5]
//! random), the blink word and the marker state flags (zero for about half
//! of the states, so the 8x8 icon is drawn). rtpt is a recording stub and
//! MFC2 returns a different value each call, so the projected points differ.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_room_geometry");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").drawAreaMap));

pub const classes = [_][:0]const u8{ "no-rooms", "current-room", "other-rooms", "invisible-first", "no-vertices", "markers", "random" };

var scenario: usize = 0;

fn word(cursor: *[*]c_uint, value: c_uint) void {
    cursor.*[0] = value;
    cursor.* += 1;
}

fn list(ctx: *zd.Ctx, cursor: *[*]c_uint, count_max: c_uint) void {
    const count = ctx.edge(c_uint, &.{ 0, 1, 2, ctx.range(c_uint, 0, count_max) });
    word(cursor, count);
    var i: c_uint = 0;
    while (i < count) : (i += 1) word(cursor, ctx.pick(c_uint, &.{ 0x00020100, 0x03020100, 0x00000000, 0x00FF0000 }));
}

fn marker(ctx: *zd.Ctx) c_uint {
    const state: c_uint = ctx.edge(c_uint, &.{ 0, 0, 1, ctx.range(c_uint, 1, 255) });
    const low: c_uint = ctx.edge(c_uint, &.{ 0x10, 0x04, 0x00, 0x01, 0x1F, 0x14, 0x08, 0x80, 0x20, 0x40 });
    return (state << 24) | (low << 16) | (ctx.int(c_uint) & 0xFF);
}

pub fn prepare(ctx: *zd.Ctx) Args {
    scenario = ctx.range(usize, 0, classes.len - 1);
    const rooms_count: c_uint = switch (scenario) {
        0 => 0,
        1 => 1,
        else => ctx.range(c_uint, 2, 3),
    };
    const memory: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(0x4000, 16)));
    for (memory[0 .. 0x4000 / 4]) |*w| w.* = ctx.int(c_uint);
    const table = memory;
    var cursor: [*]c_uint = memory + 16;
    table[0] = rooms_count;
    var room: c_uint = 0;
    while (room < rooms_count) : (room += 1) {
        const entry = table + 1 + room * 3;
        entry[0] = if (scenario == 3 and room == 0) 0 else ctx.edge(c_uint, &.{ 1, 1, 1, 2, 0x80000000 });
        entry[1] = @intCast(@intFromPtr(cursor));
        const groups: c_uint = if (scenario == 4 and room == 0) 0 else ctx.range(c_uint, 1, 3);
        word(&cursor, groups * 3);
        var k: c_uint = 0;
        while (k < groups * 6) : (k += 1) word(&cursor, ctx.int(c_uint));
        list(ctx, &cursor, 4); // triangles
        list(ctx, &cursor, 4); // quads
        list(ctx, &cursor, 4); // lines
        list(ctx, &cursor, 4); // the current room's edge lines
        const markers: c_uint = if (scenario == 5) ctx.range(c_uint, 2, 6) else ctx.edge(c_uint, &.{ 0, 1, 2 });
        word(&cursor, markers);
        k = 0;
        while (k < markers) : (k += 1) word(&cursor, marker(ctx));
    }
    const packets = ctx.allocBytes(0x8000, 16);
    const ot = ctx.allocBytes(0x40, 16);
    const ot_words: [*]c_uint = @ptrCast(@alignCast(ot));
    for (ot_words[0..16]) |*w| w.* = ctx.int(c_uint);
    const scratch: [*]volatile u32 = @ptrFromInt(zd.scratchpad);
    scratch[0] = @intCast(@intFromPtr(packets));
    scratch[1] = @intCast(@intFromPtr(ot));
    v.g.D_800F1CDC().* = ctx.int(u16);
    const flags = v.g.vs_main_stateFlags();
    var s: usize = 0;
    while (s < 256) : (s += 1) flags[s + 0x33f] = if (ctx.chance(2)) 0 else ctx.edge(u8, &.{ 1, 0x7F, 0x80, 0xFF });
    return .{table};
}

pub fn classify(args: Args) usize {
    _ = args;
    return scenario;
}
