//! zig_diff spec: func_800BC2E8 -> func_800BC2E8.func_800BC2E8 (4A0A8.c), with its statics
//! _copyVector, _vectorMagnitude and _sVectorToFixedPointVector.
//! Inputs: an 11-byte camera command with every opcode (script[0] & 15, the
//! look-at or eye by bit 4) on an arena D_800F4BA4 (random vectors); unk1E8
//! at 0x2000 half the time; script[1]'s low nibble 15, 1 or another (the
//! orbit's turn direction); script[2] never 0 for the dolly (the C divides
//! by it). The vector helpers' stubs compute as the C does (so the
//! magnitudes read real differences); _atan2FixedPoint returns angles whose
//! differences land on 0, +-2048 and their neighbours.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BC2E8));

pub const ignore_return = "commands 6 and 12..15 fall off the end of the switch (both objects leave script[0] & 15 in the result register, which the port returns); every caller (func_800BC1CC, func_800BCFB4) ignores the result";

pub const classes = [_][:0]const u8{ "set", "snap", "linear", "linear-dist", "orbit", "orbit-to", "none", "actor", "follow", "return", "actor-rel", "dolly", "none-high" };

pub fn prepare(ctx: *zd.Ctx) Args {
    const cam = ctx.alloc(v.D_800F4BA4_t);
    v.g.D_800F4BA4().* = cam;
    if (ctx.chance(2)) cam.unk1E8 = 0x2000;
    const s = ctx.allocBytes(11, 1);
    s[0] = ctx.int(u8);
    s[1] = (s[1] & 0xF0) | ctx.edge(u8, &.{ 15, 1, 0, 7 });
    if ((s[0] & 15) == 11 and s[2] == 0) s[2] = ctx.edge(u8, &.{ 1, 0xFF });
    return .{ s, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    const op = args[0][0] & 15;
    return if (op > 11) 12 else op;
}

fn vec(p: anytype) *v.VECTOR {
    return @ptrCast(@alignCast(p));
}

pub const stubs = struct {
    pub fn _vectorSubtract(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) @TypeOf(a) {
        _ = ctx;
        const x = vec(b).*;
        const y = vec(c).*;
        vec(a).vx = x.vx -% y.vx;
        vec(a).vy = x.vy -% y.vy;
        vec(a).vz = x.vz -% y.vz;
        return a;
    }
    pub fn _vectorAdd(ctx: *zd.Ctx, a: anytype, b: anytype, c: anytype) @TypeOf(a) {
        _ = ctx;
        const x = vec(b).*;
        const y = vec(c).*;
        vec(a).vx = x.vx +% y.vx;
        vec(a).vy = x.vy +% y.vy;
        vec(a).vz = x.vz +% y.vz;
        return a;
    }
    pub fn _setVectorImm(ctx: *zd.Ctx, a: anytype, p: anytype) @TypeOf(a) {
        vec(a).vx = ctx.value(c_int);
        vec(a).vy = @as(c_int, p[0]) << 12;
        vec(a).vz = ctx.value(c_int);
        return a;
    }
    pub fn _atan2FixedPoint(ctx: *zd.Ctx, y: c_int, x: c_int) c_int {
        _ = y;
        _ = x;
        const angles = [_]c_int{ 0, 2048, 1, 4095, 2047, 2049, 4096 + 2048, -1 };
        const r = ctx.value(u16);
        return if (r & 3 != 0) angles[(r >> 2) % angles.len] else ctx.value(c_int);
    }
};
