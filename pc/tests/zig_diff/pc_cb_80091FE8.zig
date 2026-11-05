//! zig_diff spec: pc_cb_80091FE8 -> room_callbacks.pc_cb_80091FE8 (2842C.c).
//! The C is `return func_80091FE8((func_80091FE8_t*)p);`. Inputs: a null object or an arena object; the func_80091FE8 stub returns any int and its call is compared.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").pc_cb_80091FE8));

pub const classes = [_][:0]const u8{ "null-object", "block-object" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(3)) return .{null};
    return .{ctx.alloc(v.D_800F1DD8_t2)};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}

pub const stubs = struct {
    pub fn func_80091FE8(ctx: *zd.Ctx, a0: anytype) c_int {
        _ = a0;
        return ctx.value(c_int);
    }
};
