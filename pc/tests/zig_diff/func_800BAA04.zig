//! zig_diff spec: func_800BAA04 -> func_800BAA04.func_800BAA04 (4A0A8.c).
//! Inputs: arg0 null or an arena byte (the C never reads it), arg1 any short
//! (also unused), and the global D_800F4C2C preset to 2 or to another byte.
//! func_8007BF04 is a recording stub that rewrites D_800F4C2C (to 2 a quarter
//! of the time, otherwise any byte), so the C's read after the call sees a
//! value a port that tests the flag before the call would miss. The return is
//! 4 when the flag is 2 after the call, else 0.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BAA04));

pub const classes = [_][:0]const u8{ "flag-2-before", "flag-other-before" };

pub const stubs = struct {
    pub fn func_8007BF04(ctx: *zd.Ctx) void {
        const flag = v.g.D_800F4C2C();
        if (ctx.value(u2) == 0) {
            flag.* = 2;
        } else {
            flag.* = ctx.value(u8);
        }
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const p: [*c]u8 = if (ctx.chance(2)) null else ctx.alloc(u8);
    v.g.D_800F4C2C().* = if (ctx.chance(2)) 2 else ctx.pick(u8, &.{ 0, 1, 3, 255 });
    return .{ p, ctx.pick(c_short, &.{ 0, 1, -1, 32767, -32768 }) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4C2C().* == 2) 0 else 1;
}
