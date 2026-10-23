//! zig_diff spec: func_80092E7C -> func_80092E7C.func_80092E7C (2842C.c).
//! Inputs: a func_80092F74_t and a func_80092F74_t2 in the arena, every byte
//! random. The four union words of arg1 are sometimes set to edge values
//! (0, -1, 0x7FFF, 0x8000, the i32 extremes) so the i16 narrowing of the
//! vector fields wraps and both signs are read. Half the runs are "aliased":
//! arg1 points at arg0's bytes, so the stores to arg0 land inside the words
//! the C still has to read, and the store order matters. The callee
//! func_80092EDC is a recording stub that changes arg0 (its vector's vy, and
//! sometimes unkC) and returns void.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092E7C));

pub const classes = [_][:0]const u8{ "distinct", "aliased" };

pub const stubs = struct {
    pub fn func_80092EDC(ctx: *zd.Ctx, arg0: anytype) void {
        const a: *v.func_80092F74_t = @ptrCast(@alignCast(arg0));
        a.unk1C.vy = ctx.value(i16);
        if (ctx.value(u1) == 1) a.unkC = ctx.value(i32);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.alloc(v.func_80092F74_t);
    const arg1: *v.func_80092F74_t2 = if (ctx.chance(2))
        @ptrCast(@alignCast(arg0))
    else
        ctx.alloc(v.func_80092F74_t2);

    if (ctx.chance(2)) {
        const edges = [_]i32{ 0, -1, 1, 0x7FFF, 0x8000, -0x8000, 0x7FFF_FFFF, std.math.minInt(i32) };
        for (0..4) |k| {
            arg1.unk4[k].s32 = ctx.pick(i32, &edges);
        }
    }
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    return if (@intFromPtr(args[0].?) == @intFromPtr(args[1].?)) 1 else 0;
}
