//! zig_diff spec: func_800CF7A8 -> func_800CF7A8.func_800CF7A8 (5BF94.c).
//! Inputs: four words at their edges (only their low bytes / nibbles are
//! packed). func_800CF3F8's stub copies the stack request into an arena
//! record, so every byte is compared.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CF7A8));

var record: ?*v.func_800CF0E8_t = null;

pub const stubs = struct {
    pub fn func_800CF3F8(ctx: *zd.Ctx, request: anytype, arg1: anytype) c_int {
        _ = arg1;
        const r: *v.func_800CF0E8_t = @ptrCast(@alignCast(request));
        record.?.* = r.*;
        return ctx.value(c_int);
    }
};

pub const classes = [_][:0]const u8{"request"};

pub fn prepare(ctx: *zd.Ctx) Args {
    record = ctx.alloc(v.func_800CF0E8_t);
    return .{ ctx.pick(c_int, &.{ 0, 0xFF, 0x1FF, -1 }), ctx.pick(c_int, &.{ 0, 0xFF, 0x1FF, -1 }), ctx.pick(c_int, &.{ 0, 9, 0xF, 0x1F, -1 }), ctx.pick(c_int, &.{ 0, 9, 0xF, 0x1F, -1 }) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
