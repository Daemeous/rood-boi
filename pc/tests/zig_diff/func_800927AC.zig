//! zig_diff spec: func_800927AC -> func_800927AC.func_800927AC (2842C.c).
//! Inputs: an arg block whose count (unk1) is 0, 1-9 or 255, so the script
//! offset takes every alignment mod 4; an id script in the arena whose bytes
//! are drawn from edges (0, -1, 1, 127, -128), so ids are negative, zero or
//! positive. func_8009195C returns NULL about a quarter of the time and
//! otherwise an arena block of random ints, so the *16 stores wrap.
//! vs_main_allocHeap hands out arena blocks big enough for every element.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800927AC));

pub const classes = [_][:0]const u8{ "empty", "odd-count", "aligned-count" };

pub const stubs = struct {
    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        return @ptrCast(ctx.allocBytes(@as(usize, size), 16));
    }

    pub fn func_8009195C(ctx: *zd.Ctx, id: anytype) ?*i32 {
        _ = id;
        if (ctx.value(u8) < 64) return null;
        const p: [*]i32 = @ptrCast(@alignCast(ctx.allocBytes(28, 4)));
        var k: usize = 0;
        while (k < 7) : (k += 1) p[k] = ctx.value(i32);
        return @ptrCast(p);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg = ctx.alloc(v.D_800F1DD8_t);
    const n: u8 = ctx.pick(u8, &.{ 0, 1, 2, 3, 4, 5, 7, 8, 9, 255 });
    arg.unk1 = n;
    const count: usize = @as(usize, n);
    const buf: [*]u8 = ctx.allocBytes(4 + count + 8, 16);
    var k: usize = 0;
    while (k < 4 + count) : (k += 1) {
        buf[k] = @bitCast(ctx.pick(i8, &.{ 0, -1, 1, 127, -128 }));
    }
    psx.put(&arg.unk4, @as(*v.D_800F1DD8_t2, @ptrCast(@alignCast(buf))));
    return .{arg};
}

pub fn classify(args: Args) usize {
    const n = args[0].?.unk1;
    if (n == 0) return 0;
    if (n % 4 != 0) return 1;
    return 2;
}
