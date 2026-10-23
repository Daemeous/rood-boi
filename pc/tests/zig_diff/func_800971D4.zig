//! zig_diff spec: func_800971D4 -> func_800971D4.func_800971D4 (2842C.c).
//! Inputs: a 272-element D_800F1BAC_t array in the arena (every element's
//! unk8.u8[0] drawn from 7, 6, 0, 255 or any byte, so 7 is common), and a
//! count D_800F227E from the edges -32768, -1, 0, 1 and 256 or a random
//! 0..256. The array is never indexed past 271, so the C's loop stays inside
//! the arena. Counts <= 0 sometimes come with a null D_800F1BAC, which the
//! C reads but never dereferences when the loop does not run. The function
//! makes no external calls, so there are no stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800971D4));

const max_elems = 272;

pub const classes = [_][:0]const u8{ "empty", "has-seven", "no-seven" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arr: [*]v.D_800F1BAC_t = @ptrCast(@alignCast(ctx.allocBytes(@sizeOf(v.D_800F1BAC_t) * max_elems, 16)));
    var i: usize = 0;
    while (i < max_elems) : (i += 1) {
        arr[i].unk8.u8[0] = ctx.pick(u8, &.{ 7, 6, 7, 0, 255 });
    }

    const n: i16 = if (ctx.chance(3))
        ctx.edge(i16, &.{ -32768, -1, 0, 1, 256 })
    else
        ctx.range(i16, 0, 256);
    v.g.D_800F227E().* = n;

    if (n <= 0 and ctx.chance(2)) {
        v.g.D_800F1BAC().* = null;
    } else {
        v.g.D_800F1BAC().* = @as(*anyopaque, @ptrCast(arr));
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const n = v.g.D_800F227E().*;
    const base = v.g.D_800F1BAC().* orelse return 0;
    if (n <= 0) return 0;
    const p: [*]v.D_800F1BAC_t = @ptrCast(@alignCast(base));
    var seven = false;
    var i: usize = 0;
    const count: usize = @intCast(n);
    while (i < count) : (i += 1) {
        if (p[i].unk8.u8[0] == 7) seven = true;
    }
    return if (seven) 1 else 2;
}
