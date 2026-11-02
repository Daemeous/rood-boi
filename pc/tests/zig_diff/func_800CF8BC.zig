//! zig_diff spec: func_800CF8BC -> func_800CF8BC.func_800CF8BC (5BF94.c).
//! No arguments: D_800F5238 keeps the harness's random byte. func_800CF3F8
//! gets a stack request (logged as a tag): its stub copies the whole request
//! into an arena record, so every byte (the ROOD_PC zeroing too) is compared.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800CF8BC));

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
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
