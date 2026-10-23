//! zig_diff spec: func_800AA620 -> func_800AA620.func_800AA620 (40564.c).
//! Inputs: a global entry D_800F4538[arg0] for arg0 in 0..16, either null or
//! a random D_800F4538_t in the arena, with random positions (edge values
//! included, so the i16 subtractions wrap). arg1 is a random SVECTOR in the
//! arena, or null when the entry is null (the C never reads it then). arg2 is
//! any c_int, edge values included. The callee func_800AA698 copies the
//! SVECTOR it is handed (the C's sp10) into an arena sink, so the sp10 values
//! the C computes are visible in the state the harness compares.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AA620));

pub const classes = [_][:0]const u8{ "entry-null", "entry-present" };

/// Arena object the callee stub copies its SVECTOR argument into.
var sink: ?*v.SVECTOR = null;

pub const stubs = struct {
    pub fn func_800AA698(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) void {
        _ = ctx;
        _ = arg0;
        _ = arg2;
        const sv: *const v.SVECTOR = @ptrCast(@alignCast(arg1));
        sink.?.* = sv.*;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    sink = ctx.alloc(v.SVECTOR);

    const idx: usize = ctx.range(usize, 0, 16);
    const present = !ctx.chance(4);
    const arr = v.g.D_800F4538();
    arr[idx] = null;

    var actor: ?*v.D_800F4538_t = null;
    if (present) {
        const a = ctx.alloc(v.D_800F4538_t);
        a.unk0.position.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
        a.unk0.position.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
        a.unk0.position.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
        arr[idx] = a;
        actor = a;
    }

    var arg1: ?*v.SVECTOR = null;
    if (present or !ctx.chance(2)) {
        const sv = ctx.alloc(v.SVECTOR);
        sv.vx = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
        sv.vy = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
        sv.vz = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
        if (actor) |a| {
            if (ctx.chance(4)) sv.vx = a.unk0.position.vx;
            if (ctx.chance(4)) sv.vz = a.unk0.position.vz;
        }
        arg1 = sv;
    }

    const arg2 = ctx.pick(c_int, &.{ 0, 1, -1, 0x7fff, 0x8000, 0x10000, -0x8001 });
    return .{ @as(c_int, @intCast(idx)), arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const idx: usize = @intCast(args[0]);
    return if (v.g.D_800F4538()[idx] == null) 0 else 1;
}
