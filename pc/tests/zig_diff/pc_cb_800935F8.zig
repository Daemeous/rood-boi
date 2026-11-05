//! zig_diff spec: pc_cb_800935F8 -> room_callbacks.pc_cb_800935F8 (2842C.c).
//! The C is `return func_800935F8((func_80092F74_t*)a, (func_80092F74_t2*)b);`. Inputs: each of the two objects null or an arena object; the func_800935F8 stub returns any int and its call (both arguments) is compared.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").pc_cb_800935F8));

pub const classes = [_][:0]const u8{ "both-null", "a-only", "b-only", "both" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const a: ?*v.func_80092B04_t = if (ctx.chance(3)) null else ctx.alloc(v.func_80092B04_t);
    const b: ?*v.func_80092B04_t2 = if (ctx.chance(3)) null else ctx.alloc(v.func_80092B04_t2);
    return .{ a, b };
}

pub fn classify(args: Args) usize {
    const a: usize = if (args[0] == null) 0 else 1;
    const b: usize = if (args[1] == null) 0 else 2;
    return a + b;
}

pub const stubs = struct {
    pub fn func_800935F8(ctx: *zd.Ctx, a0: anytype, a1: anytype) c_int {
        _ = a0;
        _ = a1;
        return ctx.value(c_int);
    }
};
