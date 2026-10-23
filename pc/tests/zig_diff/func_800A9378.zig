//! zig_diff spec: func_800A9378 -> func_800A9378.func_800A9378 (40564.c).
//! Inputs: three arguments mostly in -300..300 (sometimes the i32 and i16
//! extremes, which the stub's entries wrap around) and arg3 over the switch
//! cases 0-3 and the default (4, -1). The sp10 callee writes 0..4 entries
//! placed at the ±64 window edges and at the y thresholds (arg1-256,
//! arg1-128, arg1-D_800F4B00), so every comparison sees its two sides equal
//! often. D_800F4B00 takes edge values (0, ±1, ±128, ±256) from prepare, and
//! the stub sometimes rewrites it.

const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A9378));

pub const classes = [_][:0]const u8{ "big-arg0", "switch-default", "switch-case" };

// The arguments the sp10 callee's entries are placed around (set by prepare).
var ref0: c_int = 0;
var ref1: c_int = 0;
var ref2: c_int = 0;

const x_offsets = [_]c_int{ -65, -64, -63, -1, 0, 1, 63, 64, 65, 300 };
const y_offsets = [_]c_int{ -257, -256, -255, -129, -128, -127, -1, 0, 1, 63, 64, 100 };

fn offX(ctx: *zd.Ctx) c_int {
    return x_offsets[ctx.value(u32) % x_offsets.len];
}

fn offY(ctx: *zd.Ctx) c_int {
    return y_offsets[ctx.value(u32) % y_offsets.len];
}

pub const stubs = struct {
    pub fn func_8008D2C0(ctx: *zd.Ctx, out: anytype) c_int {
        const n: usize = ctx.value(u8) % 5;
        const q: [*]v.func_8008D2C0_t = @ptrCast(out);
        var i: usize = 0;
        while (i < n) : (i += 1) {
            q[i] = .{
                .x = @truncate(ref0 +% offX(ctx)),
                .y = @truncate(ref1 +% offY(ctx)),
                .z = @truncate(ref2 +% offX(ctx)),
                .id = ctx.value(i16),
            };
        }
        // The callee sometimes rewrites the global the C reads after the call.
        if (ctx.value(u8) % 4 == 0) v.g.D_800F4B00().* = ctx.value(i16);
        return @intCast(n);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    ref0 = if (ctx.chance(8))
        ctx.pick(c_int, &.{ 0x7fffffff, -1, 32767, -32768 })
    else
        ctx.range(c_int, -300, 300);
    ref1 = ctx.range(c_int, -300, 300);
    ref2 = if (ctx.chance(8))
        ctx.pick(c_int, &.{ 0x7fffffff, -1, 32767, -32768 })
    else
        ctx.range(c_int, -300, 300);
    v.g.D_800F4B00().* = ctx.pick(i16, &.{ 0, 1, -1, 64, 128, 256, -128, -256 });
    const arg3 = ctx.edge(c_int, &.{ 0, 1, 2, 3, 4, -1 });
    return .{ ref0, ref1, ref2, arg3 };
}

pub fn classify(args: Args) usize {
    const a0 = args[0];
    if (a0 > 1000 or a0 < -1000) return 0;
    if (args[3] == 4 or args[3] == -1) return 1;
    return 2;
}
