//! zig_diff spec: func_800BADE0 -> func_800BADE0.func_800BADE0 (4A0A8.c).
//! Inputs: a 7-byte command buffer in the arena (random bytes, so the unsigned
//! byte reads and the sign of the shorts read through vs_battle_getShort both
//! occur) and the D_800F4BF8 gate: clear a third of the time, otherwise set to
//! 1, 0x80 or 0xFF (nonzero with and without the sign bit). The callees are
//! recording stubs: func_8007BA98 may clobber the gate, func_8007B9FC returns 0
//! a quarter of the time (the C's `return 4` path) and otherwise any int, negative
//! half the time, and may clobber the gate too.
const std = @import("std");
const zd = @import("zdiff");
const common = @import("common");
const psx = common.psx;
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BADE0));

pub const classes = [_][:0]const u8{ "gate-clear", "gate-set" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) i16 {
        _ = p;
        return ctx.value(i16);
    }

    pub fn func_8007BA98(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) void {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        // The callee may set the gate; the C overwrites it with 1 afterwards.
        if (ctx.value(u2) == 0) psx.global(u8, "D_800F4BF8").* = ctx.value(u8);
    }

    pub fn func_8007B9FC(ctx: *zd.Ctx) c_int {
        const r = ctx.value(i32);
        if (ctx.value(u2) == 0) return 0;
        if (ctx.value(u3) == 0) psx.global(u8, "D_800F4BF8").* = ctx.value(u8);
        return r;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const buf = ctx.allocBytes(8, 1);
    const gate = v.g.D_800F4BF8();
    gate.* = if (ctx.chance(3)) 0 else ctx.edge(u8, &.{ 1, 0x80, 0xFF });
    return .{buf};
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (v.g.D_800F4BF8().* == 0) 0 else 1;
}
