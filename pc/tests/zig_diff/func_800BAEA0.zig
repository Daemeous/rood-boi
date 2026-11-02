//! zig_diff spec: func_800BAEA0 -> func_800BAEA0.func_800BAEA0 (4A0A8.c).
//! Inputs: a valid arg0 buffer (the C only passes arg0 + k to the getShort stub)
//! and an arg1 the C never reads. The scratchpad-free C touches only its stack
//! and the stubs' out-parameter. Stubs draw from ctx.value: the two sp10/sp20
//! coordinates are i16 (edge values -32768, 32767, 0 in "wide" calls, so the
//! differences overflow short and the int/short slip shows), getShort returns any
//! c_short (negative half the time), rsqrt returns 0, -1, -127, -128, 127, 128,
//! INT_MIN, INT_MAX or any c_int, so the `< 0` test sees equal sides and both
//! signs, and the `+= 0x7F` and `>> 7` results are checked at their edges.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_4A0A8");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800BAEA0));

pub const classes = [_][:0]const u8{ "narrow", "wide" };

/// Set by prepare; read by the stubs (the same in both runs).
var wide: bool = false;

fn coord(ctx: *zd.Ctx) i16 {
    if (!wide) return ctx.value(i16);
    return switch (ctx.value(u8) % 4) {
        0 => -32768,
        1 => 32767,
        2 => 0,
        else => ctx.value(i16),
    };
}

pub const stubs = struct {
    pub fn vs_battle_getShort(ctx: *zd.Ctx, p: anytype) c_short {
        _ = p;
        return ctx.value(c_short);
    }

    pub fn func_800BFE50(ctx: *zd.Ctx, id: anytype) c_int {
        _ = id;
        return ctx.value(c_int);
    }

    /// Writes the two coordinates the C reads back from the out-parameter.
    pub fn func_800A1108(ctx: *zd.Ctx, id: anytype, out: anytype) c_int {
        _ = id;
        const raw: ?*anyopaque = out;
        const o: *v.func_8006EBF8_t = @ptrCast(@alignCast(raw.?));
        o.unk0.unk4.vx = coord(ctx);
        o.unk0.unk4.vz = coord(ctx);
        return ctx.value(c_int);
    }

    pub fn vs_gte_rsqrt(ctx: *zd.Ctx, x: anytype) c_int {
        _ = x;
        return switch (ctx.value(u8) % 9) {
            0 => 0,
            1 => -1,
            2 => -127,
            3 => -128,
            4 => 127,
            5 => 128,
            6 => std.math.minInt(c_int),
            7 => std.math.maxInt(c_int),
            else => ctx.value(c_int),
        };
    }

    pub fn vs_battle_setStateFlag(ctx: *zd.Ctx, id: anytype, flag: anytype) void {
        _ = ctx;
        _ = id;
        _ = flag;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    wide = ctx.chance(3);
    const buf = ctx.allocBytes(16, 1);
    return .{ buf, ctx.int(c_short) };
}

pub fn classify(args: Args) usize {
    _ = args;
    return if (wide) 1 else 0;
}
