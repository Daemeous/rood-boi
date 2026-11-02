//! zig_diff spec: func_800BBE10 -> func_800BBE10.func_800BBE10 (4A0A8.c).
//! Inputs: arg0 is an edge value (0x2000 half the time, plus the 0x1FFF,
//! 0x2001, 0, 0x8000 and 0xFFFF neighbours and any u16). D_800F4BA4 points
//! at a random arena object whose unk1E8 equals arg0 in half the runs, so
//! both sides of the `unk1E8 != arg0` test occur. `_vectorAdd` is a stub
//! that writes random components into its destination, as the real callee
//! does; `func_800BBE94` is a recording stub that only records the state at
//! its call, which catches a store moved across it.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
const c = @import("c_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BBE10));

pub const classes = [_][:0]const u8{ "not-0x2000", "0x2000-unk1E8-equal", "0x2000-unk1E8-differs" };

pub const stubs = struct {
    pub fn _vectorAdd(ctx: *zd.Ctx, dst: anytype, src1: anytype, src2: anytype) [*c]c.VECTOR {
        _ = src1;
        _ = src2;
        const out: *v.VECTOR = @ptrCast(@alignCast(dst));
        out.vx = ctx.value(i32);
        out.vy = ctx.value(i32);
        out.vz = ctx.value(i32);
        return null;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const obj = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = obj;
    const arg0 = ctx.pick(u16, &.{ 0x2000, 0x2000, 0x1FFF, 0x2001, 0, 0x8000, 0xFFFF });
    obj.unk1E8 = if (ctx.chance(2)) arg0 else ctx.int(u16);
    return .{arg0};
}

pub fn classify(args: Args) usize {
    if (args[0] != 0x2000) return 0;
    const obj = v.g.D_800F4BA4().*.?;
    return if (obj.unk1E8 == args[0]) 1 else 2;
}
