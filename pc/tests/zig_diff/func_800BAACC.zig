//! zig_diff spec: func_800BAACC -> func_800BAACC.func_800BAACC (4A0A8.c).
//! Inputs: a 16-byte buffer in the arena whose byte 1, the only byte the C
//! reads, is an edge value (0, 1, 0x7F, 0x80, 0xFF) so the unsigned-to-int
//! widening and any signed reinterpretation both show up in the call trace.
//! The rest of the buffer and arg1 are random. The callee is a recording stub
//! that changes nothing: the C has no branches and no comparisons.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BAACC));

pub const classes = [_][:0]const u8{ "low-byte", "high-byte" };

pub const stubs = struct {
    pub fn func_800CEF38(ctx: *zd.Ctx, x: anytype) void {
        _ = ctx;
        _ = x;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(16, 16);
    buf[1] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    return if (args[0][1] >= 0x80) 1 else 0;
}
