//! zig_diff spec: func_800BAF6C -> func_800BAF6C.func_800BAF6C (4A0A8.c).
//! Inputs: arg0 as an edge value (0, 1, -1, the i16 extremes, 256), so both
//! the zero and nonzero tests see a sign bit and its absence. The loop runs
//! its full 1..16 range every time. The stubs return NULL for about half of
//! the func_8007CF64 calls, so both arms of the actor test run, and draw
//! their return values from ctx.value. func_800A0BE0 returns any c_uint, so
//! the `| 1` and `& ~1` paths see both bit states. func_8007D15C's result is
//! discarded by the C, so the stub only has to be consistent.
const std = @import("std");
const zd = @import("zdiff");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BAF6C));

pub const classes = [_][:0]const u8{ "arg0-zero", "arg0-positive", "arg0-negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768, 256 });
    return .{arg0};
}

pub fn classify(args: Args) usize {
    const a = args[0];
    if (a == 0) return 0;
    return if (a > 0) 1 else 2;
}

pub const stubs = struct {
    pub fn func_8007CF64(ctx: *zd.Ctx, actorId: anytype) ?*c.vs_battle_actor {
        _ = actorId;
        if (ctx.value(u1) == 1) return null;
        return @ptrCast(ctx.alloc(u64));
    }

    pub fn func_800A0BE0(ctx: *zd.Ctx, actorId: anytype) c_uint {
        _ = actorId;
        return ctx.value(c_uint);
    }

    pub fn func_8007D15C(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return ctx.value(c_int);
    }
};
