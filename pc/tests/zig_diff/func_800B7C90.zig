//! zig_diff spec: func_800B7C90 -> func_800B7C90.func_800B7C90 (4A0A8.c).
//! Inputs: an 8-byte opcode (bytes 5 and 6 any) and arg1 (unused) any i16.
//! func_800BFE50's second result (the target) is picked by prepare: below
//! 0x2000 (0, 0x1FFF), at or above it (0x2000, 0x2001, 0xFFFF) or any
//! 32-bit value; a third of the picked values get bits above 16 set, which
//! the C's & 0xFFFF drops before the >= 0x2000 test. The first result (the
//! actor) is any value: the C narrows it to u_short. Classes: the
//! func_8009FF08 branch, the func_8009FFA8 branch at 0x2000 exactly, above it.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B7C90));

pub const classes = [_][:0]const u8{ "below-0x2000", "at-0x2000", "above-0x2000" };

var target: c_int = 0;

pub const stubs = struct {
    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        // The first call is the actor (any value), the second the target.
        if (ctx.calls <= 1) return ctx.value(c_int);
        return target;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const low: c_int = switch (ctx.random.uintLessThan(u32, 4)) {
        0 => ctx.edge(c_int, &.{ 0, 0x1FFF, 0x2000, 0x2001, 0xFFFF }),
        1 => ctx.range(c_int, 0, 0x1FFF),
        2 => ctx.range(c_int, 0x2000, 0xFFFF),
        else => ctx.int(c_int),
    };
    target = if (ctx.chance(3)) low | (@as(c_int, ctx.int(u8) | 1) << 16) else low;
    const bytes = ctx.allocBytes(8, 1);
    return .{ @as([*c]u8, @ptrCast(bytes)), ctx.int(i16) };
}

pub fn classify(args: Args) usize {
    _ = args;
    const t = target & 0xFFFF;
    if (t < 0x2000) return 0;
    return if (t == 0x2000) 1 else 2;
}
