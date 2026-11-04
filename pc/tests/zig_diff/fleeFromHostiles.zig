//! zig_diff spec: func_800DB61C -> pc.flee_from_hostiles.fleeFromHostiles (D2: pc/src/battle_movement.c).
//! Inputs: an actor list of 0..5 nodes (link word at 0, actor id at 4) with
//! ids 0..15, the state's relation words (0x1A4 + id * 4) with bits 5-7
//! equal to 7 for some ids and below 7 for others, models (D_800F4538) with
//! edge or random signed positions (0x1C, 0x20) so the running sums wrap and
//! halve; the memory timer (0x114: 0, 1 or running), the tile (0x110) and the
//! mode byte (0x8A) 4 or another value. The stubs of func_800E4FE0 and
//! func_800DB4AC record their arguments and the state at the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_movement");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").fleeFromHostiles));

pub const classes = [_][:0]const u8{ "no-hostile-timer-out", "no-hostile-not-fleeing", "no-hostile-keeps-fleeing", "one-hostile", "several-hostile" };

var hostiles: usize = 0;

pub fn prepare(ctx: *zd.Ctx) Args {
    const state = ctx.allocBytes(0x400, 16);
    for (state[0..0x400]) |*byte| byte.* = ctx.int(u8);
    const table = v.g.D_800F4538();
    for (0..17) |i| {
        const model = ctx.allocBytes(0x80, 16);
        for (model[0..0x80]) |*byte| byte.* = ctx.int(u8);
        std.mem.writeInt(i16, model[0x1c..][0..2], ctx.pick(i16, &.{ 0, -1, 0x7FFF, -0x8000, 0x80, -0x80 }), .little);
        std.mem.writeInt(i16, model[0x20..][0..2], ctx.pick(i16, &.{ 0, -1, 0x7FFF, -0x8000, 0x80, -0x80 }), .little);
        table[i] = @ptrCast(model);
    }
    const count = ctx.range(usize, 0, 5);
    var previous: ?[*]u8 = null;
    var first: ?[*]u8 = null;
    hostiles = 0;
    for (0..count) |_| {
        const node = ctx.allocBytes(0x10, 16);
        for (node[0..0x10]) |*byte| byte.* = ctx.int(u8);
        const id = ctx.range(u8, 0, 15);
        std.mem.writeInt(u32, node[0..4], 0, .little);
        std.mem.writeInt(i32, node[4..8], id, .little);
        if (previous) |p| std.mem.writeInt(u32, p[0..4], @intCast(@intFromPtr(node)), .little) else first = node;
        previous = node;
        const at = 0x1a4 + @as(usize, id) * 4;
        var word = std.mem.readInt(u32, state[at..][0..4], .little);
        const hostile = ctx.chance(2);
        word = (word & ~@as(u32, 0xe0)) | (@as(u32, if (hostile) 7 else ctx.range(u8, 0, 6)) << 5);
        std.mem.writeInt(u32, state[at..][0..4], word, .little);
    }
    // Count hostiles by the same walk (an id repeated is counted per node).
    var node: ?[*]u8 = first;
    while (node) |n| {
        const id = std.mem.readInt(i32, n[4..8], .little);
        const word = std.mem.readInt(u32, state[0x1a4 + @as(usize, @intCast(id)) * 4 ..][0..4], .little);
        if ((word >> 5) & 7 == 7) hostiles += 1;
        const next = std.mem.readInt(u32, n[0..4], .little);
        node = if (next == 0) null else @ptrFromInt(next);
    }
    v.g.vs_battle_actors()[0] = if (first) |f| @ptrCast(f) else null;
    std.mem.writeInt(u16, state[0x114..][0..2], ctx.edge(u16, &.{ 0, 0, 1, 2, 90, 0xFFFF }), .little);
    state[0x8a] = if (ctx.chance(2)) 4 else ctx.int(u8);
    return .{ state, ctx.edge(c_int, &.{ 0, 1, 2, -1, 0x7FFFFFFF }) };
}

pub fn classify(args: Args) usize {
    const state = args[0];
    if (hostiles == 1) return 3;
    if (hostiles > 1) return 4;
    const timer = std.mem.readInt(u16, state[0x114..][0..2], .little);
    const left: u16 = if (timer != 0) timer - 1 else 0;
    if (left == 0) return 0;
    return if (state[0x8a] != 4) 1 else 2;
}
