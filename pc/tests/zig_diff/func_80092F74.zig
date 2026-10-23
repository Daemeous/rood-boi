//! zig_diff spec: func_80092F74 -> func_80092F74.func_80092F74 (2842C.c).
//! Inputs: a random func_80092F74_t whose unk8 is 0, positive or negative
//! (edges 0, -1, 1, 32767 and -32768 among them, so the `>= 0` test sees its
//! sides equal often), and whose unk0 guest pointer targets an i32 in the
//! arena, so the store lands in memory the harness owns. arg1 is unused by
//! the C, so it is null or an arena object.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092F74));

pub const classes = [_][:0]const u8{ "unk8-zero", "unk8-positive", "unk8-negative" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.alloc(v.func_80092F74_t);
    const target = ctx.alloc(i32);
    psx.put(&arg0.unk0, target);
    arg0.unk8 = ctx.pick(i16, &.{ 0, -1, 1, 32767, -32768 });
    const arg1: ?*v.func_80092F74_t2 = if (ctx.chance(2)) ctx.alloc(v.func_80092F74_t2) else null;
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const unk8 = args[0].?.unk8;
    if (unk8 == 0) return 0;
    if (unk8 > 0) return 1;
    return 2;
}
