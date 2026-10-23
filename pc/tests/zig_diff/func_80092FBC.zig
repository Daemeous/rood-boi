//! zig_diff spec: func_80092FBC -> func_80092FBC.func_80092FBC (2842C.c).
//! Inputs: an object in the arena (or null when the C never reads it: selector 0
//! with the 0x8000 sentinel) and a config block whose selector u16[1] is 0-3 and
//! whose limit u16[0] is an edge value, a random u16, or the 0x8000 sentinel.
//! The object's unk18 is the limit minus 1 half the time (so the counter reaches
//! the limit exactly), otherwise a small random value that is often negative.
//! The two callees are recording stubs that sometimes return 0 and sometimes
//! overwrite unk18, which the C reads back after the call.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80092FBC));

pub const classes = [_][:0]const u8{ "sentinel", "reaches-limit", "counting" };

fn asObj(p: anytype) *v.func_80092F74_t {
    const q = if (@typeInfo(@TypeOf(p)) == .optional) p.? else p;
    return @ptrCast(@alignCast(q));
}

fn touch(ctx: *zd.Ctx, p: anytype) void {
    // The callee may rewrite the object the C increments afterwards.
    const a = asObj(p);
    if (ctx.value(u1) == 1) a.unk18 = @as(i32, ctx.value(u16));
}

fn result(ctx: *zd.Ctx) c_int {
    // Zero about 40% of calls (the C tests == 0), any other value otherwise.
    if (ctx.value(u8) < 100) return 0;
    return ctx.value(c_int);
}

pub const stubs = struct {
    pub fn func_8009306C(ctx: *zd.Ctx, arg0: anytype) c_int {
        touch(ctx, arg0);
        return result(ctx);
    }
    pub fn func_80093364(ctx: *zd.Ctx, arg0: anytype) c_int {
        touch(ctx, arg0);
        return result(ctx);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.func_80092F74_t);
    const cfg = ctx.alloc(v.func_80092F74_t2);
    const limit: u16 = ctx.pick(u16, &.{ 0, 1, 2, 3, 0x8000, 0xFFFF, 0x7FFF });
    const sel: u16 = ctx.edge(u16, &.{ 0, 1, 2, 3 });
    cfg.unk4[0].u16[0] = limit;
    cfg.unk4[0].u16[1] = sel;
    obj.unk18 = if (ctx.chance(2)) @as(i32, limit) - 1 else ctx.range(i32, -70000, 70000);
    // The C never dereferences arg0 when neither callee runs and the limit is the sentinel.
    if (sel == 0 and limit == 0x8000 and ctx.chance(2)) return .{ null, cfg };
    return .{ obj, cfg };
}

pub fn classify(args: Args) usize {
    const limit = args[1].?.unk4[0].u16[0];
    if (limit == 0x8000) return 0;
    const unk18 = args[0].?.unk18;
    if (@as(i64, unk18) + 1 >= limit) return 1;
    return 2;
}
