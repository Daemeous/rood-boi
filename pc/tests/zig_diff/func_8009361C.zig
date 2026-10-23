//! zig_diff spec: func_8009361C -> func_8009361C.func_8009361C (2842C.c).
//! Inputs: arg1 is a random func_80092F74_t2 whose unk4[0].s32 (the one word
//! the C passes on) is an edge or random value, so sign and high bits vary;
//! its other unk4 entries keep random bytes. arg0 is null or a valid object
//! the C never reads. The callee is a recording stub returning a value the C
//! discards; the C returns 0 whatever it passes or the stub returns.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009361C));

pub const classes = [_][:0]const u8{ "arg0-null", "arg0-valid" };

pub const stubs = struct {
    pub fn func_800919D8(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg1 = ctx.alloc(v.func_80092F74_t2);
    arg1.unk4[0].s32 = ctx.pick(i32, &.{ 0, -1, 1, 0x7FFF, -0x8000, 0xFFFF, 0x7FFF_FFFF, -0x8000_0000 });
    const arg0: ?*v.func_80092F74_t = if (ctx.chance(2)) null else ctx.alloc(v.func_80092F74_t);
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    return if (args[0] == null) 0 else 1;
}
