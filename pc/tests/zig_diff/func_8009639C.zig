//! zig_diff spec: func_8009639C -> func_8009639C.func_8009639C (2842C.c).
//! Inputs: the count arg0 (-1..4, sometimes a large negative), the queue
//! length D_800F227E (-1..8) and a 32-entry queue whose kinds are 0 or not.
//! The stub func_80096444 (the entry removal) decrements the length and
//! rewrites the kind of the entry it was handed and sometimes another, and
//! sometimes moves the queue pointer, so a port that caches the length, the
//! pointer or an entry across the call is caught.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009639C));

pub const classes = [_][:0]const u8{ "no-loop", "no-match", "match" };

var queues: [2][*]v.D_800F1BAC_t = undefined;

pub const stubs = struct {
    pub fn func_80096444(ctx: *zd.Ctx, j: anytype) void {
        const q: [*]v.D_800F1BAC_t = @ptrCast(@alignCast(v.g.D_800F1BAC().*.?));
        const k: usize = @intCast(j);
        q[k].unk8.u8[0] = ctx.value(u8) & 1;
        q[(k + 1 + (ctx.value(u8) & 3)) & 31].unk8.u8[0] = ctx.value(u8) & 1;
        v.g.D_800F227E().* -%= 1;
        if (ctx.value(u8) & 7 == 0) v.g.D_800F1BAC().* = @ptrCast(queues[ctx.value(u8) & 1]);
    }
};

fn fill(ctx: *zd.Ctx) [*]v.D_800F1BAC_t {
    const q: [*]v.D_800F1BAC_t = @ptrCast(@alignCast(ctx.allocBytes(32 * @sizeOf(v.D_800F1BAC_t), 16)));
    const dense = ctx.chance(2);
    for (0..32) |i| {
        q[i].unk8.u8[0] = if (ctx.chance(if (dense) 2 else 6)) 0 else ctx.pick(u8, &.{ 1, 128, 255 });
    }
    return q;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    queues[0] = fill(ctx);
    queues[1] = fill(ctx);
    v.g.D_800F1BAC().* = @ptrCast(queues[0]);
    v.g.D_800F227E().* = ctx.edge(i16, &.{ -1, 0, 1, 2, 3, 5, 8 });
    const count = ctx.edge(c_int, &.{ -2147483648, -1, 0, 1, 2, 3, 4 });
    return .{count};
}

pub fn classify(args: Args) usize {
    if (args[0] <= 0) return 0;
    const n = v.g.D_800F227E().*;
    const q: [*]v.D_800F1BAC_t = @ptrCast(@alignCast(v.g.D_800F1BAC().*.?));
    var j: i16 = 0;
    while (j < n) : (j += 1) {
        if (q[@intCast(j)].unk8.u8[0] == 0) return 2;
    }
    return 1;
}
