//! zig_diff spec: func_800A9FD0 -> func_800A9FD0.func_800A9FD0 (40564.c).
//! Inputs: an actor from D_800F4538 at a random slot 0..16 with random pitch,
//! facing and roll, and an SVECTOR whose components sit at the actor's value
//! plus an edge difference (2048, -2048, their neighbours, 0, 4096 multiples,
//! the i16 extremes), so the three wrap tests see their equal sides often and
//! both branches of each. arg2 is -1 half the time (the early return), else
//! an edge value that the C stores into unk5C0 around the call. The callee
//! copies the vector it is passed into the actor (unk1814..unk1818), so the
//! adjusted sp10 components reach the state the harness compares. The callee
//! sometimes overwrites unk5C0, so a port that does not restore the saved
//! value after the call differs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A9FD0));

pub const classes = [_][:0]const u8{ "arg2-is-minus-one", "store-no-wrap", "axis-wraps" };

pub const stubs = struct {
    pub fn func_800AAA88(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) void {
        _ = arg2;
        // The callee reads the vector it is given (the C's sp10, a stack
        // object the harness does not compare by content) and keeps its
        // components in the actor, so every wrap result is observable.
        const sv: *v.SVECTOR = @ptrCast(@alignCast(arg1));
        const obj = v.g.D_800F4538()[@intCast(arg0)].?;
        obj.unk1814 = sv.vx;
        obj.unk1816 = sv.vy;
        obj.unk1818 = sv.vz;
        // The callee may change the actor's unk5C0; the C restores its own saved value.
        if (ctx.value(u1) == 1) {
            obj.unk5C0 = ctx.value(u16);
        }
    }
};

fn diffEdge(ctx: *zd.Ctx) i32 {
    return ctx.pick(i32, &.{ 2048, -2048, 2047, -2047, 2049, -2049, 0, 1, -1, 4096, -4096, 6144, -6144, 32767, -32768 });
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    const idx = ctx.range(c_int, 0, 16);
    v.g.D_800F4538()[@intCast(idx)] = obj;

    obj.unk0.pitch = ctx.int(i16);
    obj.unk0.facing = ctx.int(i16);
    obj.unk0.roll = ctx.int(i16);
    obj.unk5C0 = ctx.int(u16);

    const sv = ctx.alloc(v.SVECTOR);
    sv.vx = @truncate(@as(i32, obj.unk0.pitch) + diffEdge(ctx));
    sv.vy = @truncate(@as(i32, obj.unk0.facing) + diffEdge(ctx));
    sv.vz = @truncate(@as(i32, obj.unk0.roll) + diffEdge(ctx));

    const arg2: c_int = if (ctx.chance(2))
        -1
    else
        ctx.pick(c_int, &.{ 0, 1, -2, 0x7FFF, 0xFFFF, 0x10000, std.math.maxInt(c_int), std.math.minInt(c_int) });

    return .{ idx, sv, arg2 };
}

pub fn classify(args: Args) usize {
    if (args[2] == -1) return 0;
    const obj = v.g.D_800F4538()[@intCast(args[0])].?;
    const sv = args[1].?;
    const dx: i16 = sv.vx -% obj.unk0.pitch;
    const dy: i16 = sv.vy -% obj.unk0.facing;
    const dz: i16 = sv.vz -% obj.unk0.roll;
    if (dx > 2048 or dx < -2048 or dy > 2048 or dy < -2048 or dz > 2048 or dz < -2048) return 2;
    return 1;
}
