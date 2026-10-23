//! zig_diff spec: func_800935F8 -> func_800935F8.func_800935F8 (2842C.c).
//! Inputs: arg1 is an arena func_80092F74_t2 whose unk4[0].s32 is an edge
//! value (0, -1, 1, the i32 extremes, 0xFFFF, or random bytes), and arg0 is
//! null or an arena block; the C never reads arg0. The callee
//! func_8009291C is a recording stub that returns any c_int, which the C
//! ignores, and it changes nothing the C reads back.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800935F8));

pub const classes = [_][:0]const u8{ "arg0-null", "arg0-set" };

pub const stubs = struct {
    pub fn func_8009291C(ctx: *zd.Ctx, arg: anytype) c_int {
        _ = arg;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg1 = ctx.alloc(v.func_80092F74_t2);
    arg1.unk4[0].s32 = ctx.pick(i32, &.{ 0, -1, 1, 0x7FFF_FFFF, -0x8000_0000, 0xFFFF });
    const arg0: ?*v.func_80092F74_t = if (ctx.chance(2)) null else ctx.alloc(v.func_80092F74_t);
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
