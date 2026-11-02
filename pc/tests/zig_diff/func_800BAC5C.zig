//! zig_diff spec: func_800BAC5C -> func_800BAC5C.func_800BAC5C (4A0A8.c).
//! Inputs: a u_char block whose byte 5 is the divisor (1 half the time, else
//! 2..255, never 0), and any short arg1 (the C never reads it). The two
//! func_800A1108 out-params get vx/vz from edge-or-random shorts, so the
//! differences wrap to short. vs_gte_rsqrt returns 256 (the `< 0x100`
//! equality, with divisor 1), 255, negatives, the int extremes, 0, and 512
//! (256 at divisor 2), else any int. getShort, ratan2 and the state-flag
//! setter use the default stubs.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BAC5C));

pub const classes = [_][:0]const u8{ "divisor-one", "divisor-other" };

pub fn classify(args: Args) usize {
    return if (args[0][5] == 1) 0 else 1;
}

pub fn prepare(ctx: *zd.Ctx) Args {
    const bytes = ctx.allocBytes(8, 1);
    bytes[5] = if (ctx.chance(2)) 1 else ctx.range(u8, 2, 255);
    const arg0: [*c]u8 = bytes;
    return .{ arg0, ctx.int(c_short) };
}

/// A vx/vz value: the extremes and 0 often, any i16 otherwise.
fn coord(ctx: *zd.Ctx) i16 {
    return switch (ctx.value(u2)) {
        0 => -32768,
        1 => 32767,
        2 => 0,
        else => ctx.value(i16),
    };
}

pub const stubs = struct {
    /// Writes both out-parameters' vx and vz (the C reads them back).
    pub fn func_800A1108(ctx: *zd.Ctx, id: anytype, out: anytype) c_int {
        _ = id;
        const sv: *v.func_8006EBF8_t = @ptrCast(@alignCast(out));
        sv.unk0.unk4.vx = coord(ctx);
        sv.unk0.unk4.vz = coord(ctx);
        return ctx.value(c_int);
    }

    /// The C tests the result against 0x100 and sign: return those edges.
    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return switch (ctx.value(u3)) {
            0 => 256,
            1 => 255,
            2 => -1,
            3 => std.math.maxInt(c_int),
            4 => std.math.minInt(c_int),
            5 => 0,
            6 => 512,
            else => ctx.value(c_int),
        };
    }
};
