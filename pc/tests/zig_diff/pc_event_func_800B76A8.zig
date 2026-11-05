//! zig_diff spec: pc_event_func_800B76A8 -> event_callbacks.pc_event_func_800B76A8 (4A0A8.c).
//! The C is `(void)script; (void)slot; return func_800B76A8(script);`. Inputs: a null script or an arena block, and an edge or any slot (which the C never reads); the func_800B76A8 stub returns any int and its call is compared.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").pc_event_func_800B76A8));

pub const classes = [_][:0]const u8{ "null-script", "block-script" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const script: [*c]u8 = if (ctx.chance(3)) null else ctx.allocBytes(64, 16);
    return .{ script, ctx.pick(c_short, &.{ 0, 1, -1, 255, 256, 32767, -32768 }) };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}

pub const stubs = struct {
    pub fn func_800B76A8(ctx: *zd.Ctx, a0: anytype) c_int {
        _ = a0;
        return ctx.value(c_int);
    }
};
