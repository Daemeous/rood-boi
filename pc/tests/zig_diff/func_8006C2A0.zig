//! zig_diff spec: func_8006C2A0 -> func_8006C2A0.func_8006C2A0 (146C.c).
//! The C takes no arguments and has no globals of its own: its inputs are the
//! values the two callees return. Each run picks a mode: every returned value
//! has bit 2 clear, every one has it set, or they are random (so the sign bit
//! and bit 2 vary independently). The callee stubs return values from
//! ctx.value only, so both runs see the same sequence. func_800A087C changes
//! nothing the C reads back, so it is a recording void stub.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_146C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8006C2A0));

pub const classes = [_][:0]const u8{ "bit2-clear", "bit2-set", "bit2-mixed" };

var mode: u8 = 0;

pub const stubs = struct {
    pub fn func_800A0BE0(ctx: *zd.Ctx, i: anytype) c_uint {
        _ = i;
        const r = ctx.value(c_uint);
        return switch (mode) {
            0 => r & ~@as(c_uint, 4),
            1 => r | 4,
            else => r,
        };
    }

    pub fn func_800A087C(ctx: *zd.Ctx, i: anytype, flags: anytype) void {
        _ = ctx;
        _ = i;
        _ = flags;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    mode = ctx.edge(u8, &.{ 0, 1, 2 });
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return switch (mode) {
        0 => 0,
        1 => 1,
        else => 2,
    };
}
