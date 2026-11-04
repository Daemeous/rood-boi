//! zig_diff spec: func_800B4594 -> pc.build_actor_packets.buildActorPackets (D2: pc/src/actor_packets.c).
//! Inputs: the scratchpad's packet cursor (word 0), ordering table (word 4)
//! and texture word (0x390), all random; blocks for the projected vertices,
//! the actor and the model, whose polygon pointer at +0x3C is a random
//! guest word. pc_actor_packets' stub records its eight arguments (the
//! halfword counts pointer at actor + 2, the depth array at projected +
//! 0xC00, the texture word split in two) and returns an arena address,
//! which the function must store back as the cursor; the hook stub records
//! its arguments.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").buildActorPackets));

pub const classes = [_][:0]const u8{"build"};

pub const stubs = struct {
    pub fn pc_actor_packets(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype, e: anytype, f: anytype, g: anytype, h: anytype) [*c]c_uint {
        _ = .{ a, b, c, d, e, f, g, h };
        return @ptrCast(@alignCast(ctx.allocBytes(16, 16)));
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const scratch: [*]volatile u32 = @ptrFromInt(0x1f800000);
    scratch[0] = ctx.int(u32);
    scratch[1] = ctx.int(u32);
    scratch[0x390 / 4] = ctx.int(u32);
    const projected: [*]c_uint = @ptrCast(@alignCast(ctx.allocBytes(0x20, 16)));
    const actor = ctx.allocBytes(0x20, 16);
    const model = ctx.allocBytes(0x80, 16);
    for (model[0..0x80]) |*byte| byte.* = ctx.int(u8);
    return .{ projected, actor, if (ctx.chance(2)) null else @ptrCast(model), model };
}

pub fn classify(args: Args) usize {
    _ = args;
    return 0;
}
