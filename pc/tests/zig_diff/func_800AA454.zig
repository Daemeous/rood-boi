//! zig_diff spec: func_800AA454 -> func_800AA454.func_800AA454 (40564.c).
//! The wrapper has no branches. The inputs cover every table slot the C can
//! read (0, 16 and the middle), with the object at that slot holding random
//! bytes so unk5C0 takes any u16. arg1 is null or an arena block, arg2 takes
//! the c_int edges. The callee's stub writes the slot's unk5C0 sometimes, so
//! a port that re-reads the field after the call differs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_40564");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800AA454));

pub const classes = [_][:0]const u8{ "slot-0", "slot-16", "middle-slot" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.range(c_int, 0, 16);
    const obj = ctx.alloc(v.D_800F4538_t);
    const table = v.g.D_800F4538();
    table[@intCast(arg0)] = obj;
    const arg1: ?*v.func_8006EBF8_t_fields = if (ctx.chance(4)) null else ctx.alloc(v.func_8006EBF8_t_fields);
    const arg2 = ctx.pick(c_int, &.{ 0, -1, 1, std.math.maxInt(c_int), std.math.minInt(c_int) });
    return .{ arg0, arg1, arg2 };
}

pub fn classify(args: Args) usize {
    const i = args[0];
    if (i == 0) return 0;
    if (i == 16) return 1;
    return 2;
}

pub const stubs = struct {
    pub fn func_800AA490(ctx: *zd.Ctx, a: anytype, b: anytype, c_: anytype, d: anytype) void {
        _ = b;
        _ = c_;
        _ = d;
        // The callee may change the slot's object after the C has read unk5C0.
        if (ctx.value(u1) == 1) {
            const slot: usize = @intCast(a);
            v.g.D_800F4538()[slot].?.unk5C0 = ctx.value(u16);
        }
    }
};
