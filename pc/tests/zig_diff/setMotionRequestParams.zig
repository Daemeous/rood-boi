//! zig_diff spec: func_800B64A8 -> pc.motion_request.setMotionRequestParams (D2: pc/src/battle_motion.c).
//! Inputs: the actor's slot a random-filled block (0x1900 bytes, the request
//! block at 0x1848 inside it); a and b (edges or any int). The stub of
//! func_800B64CC records its four arguments (c must equal a) and writes the
//! kind word 0x1848 to a nonzero value, so the port's clearing of byte 0x1848
//! afterwards is seen; the stub also writes 0x1849 so a clear of more than the
//! one byte is seen.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_motion");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setMotionRequestParams));

pub const classes = [_][:0]const u8{ "any" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 16);
    const model = ctx.allocBytes(0x1900, 16);
    for (model[0..0x1900]) |*byte| byte.* = ctx.int(u8);
    v.g.D_800F4538()[@intCast(id)] = @ptrCast(model);
    const a = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000, 0x10000 });
    const b = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000 });
    return .{ id, a, b };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}

pub const stubs = struct {
    pub fn func_800B64CC(ctx: *zd.Ctx, id: anytype, a: anytype, b: anytype, cc: anytype) void {
        _ = a;
        _ = b;
        _ = cc;
        const model: [*]u8 = @ptrCast(v.g.D_800F4538()[@intCast(id)].?);
        model[0x1848] = 1 | ctx.value(u8);
        model[0x1849] = 1 | ctx.value(u8);
    }
};
