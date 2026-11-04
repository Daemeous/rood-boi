//! zig_diff spec: func_800B6530 -> pc.motion_request.setMotionRequestAnimatedTurn (D2: pc/src/battle_motion.c).
//! Inputs: the actor's slot a random-filled block (0x1900 bytes); a position
//! pointer to two arena words; b (edges or any int). The stub of func_800B653C
//! records (id, position, b, c) (c must equal b) and writes the kind word
//! 0x1848 to 0x0A0B0C03 (upper bytes nonzero: a byte store of 4 leaves them, the word store clears them).
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_battle_motion");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").setMotionRequestAnimatedTurn));

pub const classes = [_][:0]const u8{ "any" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const id = ctx.range(c_int, 0, 16);
    const model = ctx.allocBytes(0x1900, 16);
    for (model[0..0x1900]) |*byte| byte.* = ctx.int(u8);
    v.g.D_800F4538()[@intCast(id)] = @ptrCast(model);
    const position: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(8, 16)));
    position[0] = ctx.int(c_uint);
    position[1] = ctx.int(c_uint);
    const b = ctx.pick(c_int, &.{ 0, 1, -1, 0x7FFFFFFF, -0x80000000, 0xFFFF, 0x10000 });
    return .{ id, position, b };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}

pub const stubs = struct {
    pub fn func_800B653C(ctx: *zd.Ctx, id: anytype, position: anytype, b: anytype, cc: anytype) void {
        _ = ctx;
        _ = position;
        _ = b;
        _ = cc;
        const model: [*]u8 = @ptrCast(v.g.D_800F4538()[@intCast(id)].?);
        @as(*c_uint, @ptrCast(@alignCast(model + 0x1848))).* = 0x0A0B0C03;
    }
};
