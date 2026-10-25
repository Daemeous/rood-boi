//! zig_diff spec: func_8009F990 -> func_8009F990.func_8009F990 (30DB0.c).
//! Inputs: arg0 is -1 in about a third of the runs (the 17-slot loop), and
//! otherwise an edge value (0, 1, 16, 17, -2, 65535, INT_MAX, INT_MIN) or any
//! random int, so negative values other than -1 and the 65535 slip are seen.
//! arg1 is an arena object or NULL. The callee func_8009F9F4 is a recording
//! stub that writes fields of its object and skips a NULL pointer, so a port
//! that drops or changes the pointer argument, or reads the object back, is
//! caught.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_30DB0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009F990));

pub const classes = [_][:0]const u8{ "other-arg0", "all-slots" };

pub const stubs = struct {
    pub fn func_8009F9F4(ctx: *zd.Ctx, i: anytype, p: anytype) void {
        _ = i;
        if (p == null) return;
        const o: *v.D_800F4538_unk64 = @ptrCast(@alignCast(p));
        if (ctx.value(u1) == 1) o.unk0 = ctx.value(u8);
        if (ctx.value(u1) == 1) o.unk3 = ctx.value(u8);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj: ?*v.D_800F4538_unk64 = if (ctx.chance(6)) null else ctx.alloc(v.D_800F4538_unk64);
    const arg0: c_int = if (ctx.chance(3))
        -1
    else
        ctx.pick(c_int, &.{ 0, 1, 16, 17, -2, 65535, 0x7fffffff, -2147483648 });
    return .{ arg0, obj };
}

pub fn classify(args: Args) usize {
    return if (args[0] == -1) 1 else 0;
}
