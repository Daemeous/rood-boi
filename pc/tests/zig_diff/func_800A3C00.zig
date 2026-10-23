//! zig_diff spec: func_800A3C00 -> func_800A3C00.func_800A3C00 (3A1A0.c).
//! Inputs: a D_800F45E0_t in the arena or a null slot (the guard the C
//! tests for), its unk5C and unk5E bytes and unk1E halfword from edge sets
//! (unk1E negative, zero and positive), and arg1 from edge values. The
//! callee func_800A3C34 is a recording stub that returns null or a fresh
//! arena object. The C reads nothing back from the callee's side effects,
//! so the stub writes no state.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A3C00));

pub const classes = [_][:0]const u8{ "null", "unk1E-negative", "unk1E-zero", "unk1E-positive" };

pub const stubs = struct {
    pub fn func_800A3C34(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) ?*v.D_800F4538_t {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        if (ctx.value(u8) < 64) return null; // the callee may fail
        return ctx.alloc(v.D_800F4538_t);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: ?*v.D_800F45E0_t = if (ctx.chance(6)) null else blk: {
        const obj = ctx.alloc(v.D_800F45E0_t);
        obj.unk1E = ctx.pick(i16, &.{ 0, 1, -1, 32767, -32768 });
        obj.unk5C = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
        obj.unk5E = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
        break :blk obj;
    };
    const arg1 = ctx.pick(c_uint, &.{ 0, 1, 0x7FFF_FFFF, 0x8000_0000, 0xFFFF_FFFF });
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const obj = args[0] orelse return 0;
    if (obj.unk1E < 0) return 1;
    if (obj.unk1E == 0) return 2;
    return 3;
}
