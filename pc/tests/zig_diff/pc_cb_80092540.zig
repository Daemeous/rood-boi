//! zig_diff spec: pc_cb_80092540 -> room_callbacks.pc_cb_80092540 (2842C.c).
//! The C is `(void)p; return func_80092540();`. Inputs: a null object or an arena object, which the C never reads; the func_80092540 stub returns any int.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").pc_cb_80092540));

pub const classes = [_][:0]const u8{ "null-object", "block-object" };

pub fn prepare(ctx: *zd.Ctx) Args {
    if (ctx.chance(3)) return .{null};
    return .{ctx.alloc(v.D_800F1DD8_t2)};
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}

pub const stubs = struct {
    pub fn func_80092540(ctx: *zd.Ctx) c_int {
        return ctx.value(c_int);
    }
};
