//! zig_diff spec: func_800A9EB4 -> func_800A9EB4.func_800A9EB4 (40564.c).
//! Inputs: a random D_800F4538_t in the arena, installed in a random slot
//! 0..16 of the global table, with a random facing (i16). arg1 is built so
//! that arg1 - facing lands on the wrap bounds (+-2048 exactly, just past
//! them, and far outside the short range), so both comparisons see equal
//! sides often. arg2 is -1 (the early return) or an edge value stored into
//! unk5C0 (u16) around the call. The callee is a recording stub that
//! sometimes overwrites unk5C0, so a port that restores it wrongly or
//! stores at the wrong point is caught.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A9EB4));

pub const classes = [_][:0]const u8{ "early-return", "edge-equal", "wrapped-high", "wrapped-low", "in-range" };

const edges = [_]i32{ 2048, -2048, 2049, -2049, 2047, -2047, 0, 1, -1, 4095, -4095, 30000, -30000, 65535, -65536 };

/// The object the table slot points at, for the callee stub.
var actor: ?*v.D_800F4538_t = null;

pub const stubs = struct {
    pub fn func_800AA984(ctx: *zd.Ctx, slot: anytype, arg1: anytype, arg2: anytype) void {
        _ = slot;
        _ = arg1;
        _ = arg2;
        // The callee may change the slot's unk5C0 the C restores afterwards.
        if (ctx.value(u1) == 1) actor.?.unk5C0 = ctx.value(u16);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4538_t);
    actor = obj;
    const idx = ctx.range(u8, 0, 16);
    v.g.D_800F4538()[idx] = obj;
    obj.unk0.facing = ctx.pick(i16, &.{ 0, 1, -1, 2048, -2048, 32767, -32768 });
    const want: i32 = if (ctx.chance(2)) ctx.edge(i32, &edges) else @as(i32, ctx.int(i16));
    // arg1 - facing == want in int, so the C's short arithmetic sees want.
    const arg1: i16 = @truncate(want + @as(i32, obj.unk0.facing));
    const arg2: c_int = if (ctx.chance(4))
        -1
    else
        ctx.pick(c_int, &.{ 0, 1, -2, 65535, 65536, -65536, 0x7FFF_FFFF, -0x8000_0000 });
    return .{ @as(c_int, idx), arg1, arg2 };
}

pub fn classify(args: Args) usize {
    if (args[2] == -1) return 0;
    const obj = v.g.D_800F4538()[@intCast(args[0])].?;
    const r: i16 = @truncate(@as(i32, args[1]) - @as(i32, obj.unk0.facing));
    if (r == 2048 or r == -2048) return 1;
    if (r > 2048) return 2;
    if (r < -2048) return 3;
    return 4;
}
