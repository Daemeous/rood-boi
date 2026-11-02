//! zig_diff spec: func_800B8510 -> func_800B8510.func_800B8510 (4A0A8.c).
//! Inputs: arg0 is a 16-byte arena block; the C reads only its byte 5, which
//! is zero (a third of the runs), small, or has the sign bit set. arg1 is any
//! short (the C never reads it). The callees decide the branches:
//! func_800BFE50 returns 0x2000 about a third of the time (the -2 default
//! path) and otherwise any 32-bit value, so the call arguments span the full
//! range. vs_battle_getShort returns any short. func_8009FC60 returns an int
//! as its prototype declares and changes no state the C reads back.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B8510));

pub const classes = [_][:0]const u8{ "a5-zero", "a5-small", "a5-sign" };

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }

    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        if (ctx.value(u8) < 85) return 0x2000;
        return ctx.value(c_int);
    }

    pub fn func_8009FC60(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype, d: anytype) c_int {
        _ = a;
        _ = b;
        _ = c;
        _ = d;
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0: [*c]u8 = ctx.allocBytes(16, 16);
    arg0[5] = if (ctx.chance(3)) 0 else ctx.pick(u8, &.{ 1, 127, 128, 255 });
    return .{ arg0, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const b = args[0][5];
    if (b == 0) return 0;
    return if (b >= 128) 2 else 1;
}
