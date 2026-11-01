//! zig_diff spec: func_80093AB4 -> func_80093AB4.func_80093AB4 (2842C.c).
//! Inputs: the queue pointer D_800F1BAC points at an arena block or at a
//! global; the counters keep their random fill. vs_main_bzero is a recording
//! stub (its pointer and size are compared); it also writes the counters,
//! so a store moved after the call is caught.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80093AB4));

pub const classes = [_][:0]const u8{"reset"};

pub const stubs = struct {
    pub fn vs_main_bzero(ctx: *zd.Ctx, p: anytype, n: anytype) void {
        _ = p;
        _ = n;
        v.g.D_800F2270().* = ctx.value(i16);
        v.g.D_800F227A().* = ctx.value(i16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    v.g.D_800F1BAC().* = @ptrCast(ctx.allocBytes(32 * @sizeOf(v.D_800F1BAC_t), 16));
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
