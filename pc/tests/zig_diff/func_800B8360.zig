//! zig_diff spec: func_800B8360 -> func_800B8360.func_800B8360 (4A0A8.c).
//! Inputs: a 16-byte arena block as arg0, so the bytes at arg0+1, +3 and +5
//! feed the getShort and func_800BFE50 calls, and arg0[7] picks the scratch
//! count: 0 (the 0x1000 default), 1 (the smallest nonzero value), 0x7F, 0x80
//! and 0xFF (the sign-bit split of a nonzero byte). arg1 is random and unused
//! by the C. The stubs return random shorts and ints. func_8009FC60 copies its
//! vector and count into an arena log, so a wrong vz or count shows in the
//! state compare.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800B8360));

pub const classes = [_][:0]const u8{ "count-zero", "count-one", "count-other" };

/// Set by prepare: the arena log func_8009FC60 writes its inputs to.
var log: *[4]i16 = undefined;

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }

    pub fn func_800BFE50(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return ctx.value(c_int);
    }

    pub fn func_8009FC60(ctx: *zd.Ctx, a: anytype, b: anytype, p: anytype, n: anytype) c_int {
        _ = a;
        _ = b;
        const s: *const v.SVECTOR = @ptrCast(@alignCast(p));
        log.* = .{ s.vx, s.vy, s.vz, @truncate(n) };
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = ctx.allocBytes(16, 16);
    arg0[7] = ctx.pick(u8, &.{ 0, 1, 0x7F, 0x80, 0xFF });
    log = ctx.alloc([4]i16);
    const arg1 = ctx.pick(c_short, &.{ 0, -1, 0x7FFF, -32768, 1 });
    return .{ @as([*c]u8, arg0), arg1 };
}

pub fn classify(args: Args) usize {
    const sel = args[0][7];
    if (sel == 0) return 0;
    if (sel == 1) return 1;
    return 2;
}
