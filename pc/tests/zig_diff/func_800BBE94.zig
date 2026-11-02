//! zig_diff spec: func_800BBE94 -> func_800BBE94.func_800BBE94 (4A0A8.c).
//! Inputs: the global D_800F4BA4 points at an arena object whose unk1E8 is
//! 0x2000 (the sentinel path) or any other u16 (the call path, including 0,
//! 0x7FFF, 0x8000 and 0xFFFF). The callee func_800A1108 writes the three
//! SVECTOR components of its out-parameter with any i16 (so the products
//! take both signs and the unsigned slip shows), returns any c_int (the C
//! ignores it), and sometimes repoints D_800F4BA4 at another arena object,
//! which the C's stores after the call must follow.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BBE94));

pub const classes = [_][:0]const u8{ "sentinel", "call" };

pub const stubs = struct {
    pub fn func_800A1108(ctx: *zd.Ctx, sel: c_int, out: ?*anyopaque) c_int {
        _ = sel;
        const o: *v.func_8006EBF8_t = @ptrCast(@alignCast(out.?));
        o.unk0.unk4.vx = ctx.value(i16);
        o.unk0.unk4.vy = ctx.value(i16);
        o.unk0.unk4.vz = ctx.value(i16);
        // The callee may repoint the global; the C stores through it afterwards.
        if (ctx.value(u1) == 1) {
            const fresh = ctx.alloc(v.D_800F4BA4_t);
            v.g.D_800F4BA4().* = fresh;
        }
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4BA4_t);
    obj.unk1E8 = if (ctx.chance(3))
        0x2000
    else
        ctx.pick(u16, &.{ 0, 1, 0x1FFF, 0x2001, 0x7FFF, 0x8000, 0xFFFF });
    v.g.D_800F4BA4().* = obj;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4BA4().*.?.unk1E8 == 0x2000) 0 else 1;
}
