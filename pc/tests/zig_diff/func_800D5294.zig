//! zig_diff spec: func_800D5294 -> func_800D5294.func_800D5294 (5BF94.c).
//! Inputs: a pointer to a c_int the C reads, in one of three memory regions the
//! harness owns: a two-entry arena object (values from the edge list or random),
//! the PS1 scratchpad, or PS1 address 0 (mapped at 0x80000000). The value covers
//! zero, one, minus one, the 16-bit edges, both int extremes and random bits, so
//! sign, width and wrapping of the store are all visible. The global D_800F5618
//! starts from the same edge list, so a port that skips the store is caught too.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D5294));

pub const classes = [_][:0]const u8{ "arena", "scratchpad", "low-ram" };

const edges = [_]c_int{
    0,
    1,
    -1,
    0x7FFF,
    -0x8000,
    0xFFFF,
    std.math.maxInt(c_int),
    std.math.minInt(c_int),
};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F5618().* = ctx.pick(c_int, &edges);
    const sel = ctx.range(u8, 0, 2);
    const p: [*c]c_int = switch (sel) {
        0 => blk: {
            const buf = ctx.alloc([2]c_int);
            buf[0] = ctx.pick(c_int, &edges);
            buf[1] = ctx.int(c_int);
            break :blk @ptrCast(&buf[0]);
        },
        1 => @ptrFromInt(zd.scratchpad + 4 * ctx.range(usize, 0, 1022)),
        else => @ptrFromInt(zd.low_ram + 4 * ctx.range(usize, 0, 1022)),
    };
    return .{p};
}

pub fn classify(args: Args) usize {
    const a = @intFromPtr(args[0]);
    if (a >= zd.low_ram and a < zd.low_ram + zd.low_ram_size) return 2;
    if (a >= zd.scratchpad and a < zd.scratchpad + zd.scratchpad_size) return 1;
    return 0;
}
