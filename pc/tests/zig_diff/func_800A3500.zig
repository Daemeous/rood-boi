//! zig_diff spec: func_800A3500 -> func_800A3500.func_800A3500 (3A1A0.c).
//! Inputs: two ints (mostly in i16 range, with edges such as 32768, 65535
//! and 0x7FFFFFFF), and the 16-slot actor table D_800F45E0 with null slots
//! and actors whose unk1C/unk20 match or miss arg0/arg1. The callee's 15-bit
//! result h (from ret_value, with bits 14 and 15 both set and clear) is
//! matched by unk1E - 0x80 below, equal to, or above h, with edge values for
//! unk1E. A quiet mode leaves no slot matching. The callee sometimes rewrites
//! an actor's unk1E, as the real function may.

const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_3A1A0");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800A3500));

pub const classes = [_][:0]const u8{ "no-match", "match-above", "match-below", "match-equal" };

/// The value the callee returns in most calls; prepare sets it, so classify
/// can predict h.
var ret_value: i16 = 0;

/// h as the C computes it: bits 0..14 of the returned value, sign-extended.
fn sext15(x: i16) i32 {
    const u: u16 = @bitCast(x);
    const s: i16 = @bitCast(u << 1);
    return @as(i32, s >> 1);
}

fn argValue(ctx: *zd.Ctx) c_int {
    if (ctx.chance(5)) return ctx.pick(c_int, &.{ 0, 1, -1, 32767, -32768, 32768, 65535, -32769, 0x7FFFFFFF, -0x8000_0000 });
    return @intCast(ctx.range(i16, -32768, 32767));
}

pub const stubs = struct {
    pub fn func_8008DA24(ctx: *zd.Ctx, a: anytype, b: anytype) c_short {
        _ = a;
        _ = b;
        // The callee may rewrite one actor's unk1E before the loop reads it.
        if (ctx.value(u4) == 0) {
            const arr = v.g.D_800F45E0();
            const idx = ctx.value(u4);
            if (arr[idx]) |actor| actor.unk1E = ctx.value(i16);
        }
        if (ctx.value(u2) == 0) return @intCast(ctx.value(i16));
        return @intCast(ret_value);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const arg0 = argValue(ctx);
    const arg1 = argValue(ctx);
    ret_value = if (ctx.chance(3))
        ctx.pick(i16, &.{ 0, 1, -1, 0x3FFF, -0x4000, 0x4000, -0x3FFF, 0x7FFF, -0x8000 })
    else
        ctx.int(i16);
    const h = sext15(ret_value);
    const quiet = ctx.chance(5);
    const t0: i16 = @truncate(arg0);
    const t1: i16 = @truncate(arg1);
    const arr = v.g.D_800F45E0();
    for (arr) |*slot| {
        if (ctx.chance(3)) {
            slot.* = null;
            continue;
        }
        const a = ctx.alloc(v.D_800F45E0_t);
        slot.* = a;
        const hit = !quiet and !ctx.chance(3);
        a.unk1C = if (hit) t0 else t0 +% 1;
        a.unk20 = if (hit and !ctx.chance(4)) t1 else ctx.int(i16);
        a.unk1E = if (ctx.chance(2))
            @intCast(h + 128)
        else if (ctx.chance(2))
            @intCast(h + 128 + ctx.range(i32, -2, 2))
        else
            ctx.pick(i16, &.{ 0, 127, 128, 129, -32768, 32767 });
    }
    return .{ arg0, arg1 };
}

pub fn classify(args: Args) usize {
    const h = sext15(ret_value);
    const arr = v.g.D_800F45E0();
    var result: usize = 0;
    for (arr) |slot| {
        const a = slot orelse continue;
        if (@as(c_int, a.unk1C) != args[0] or @as(c_int, a.unk20) != args[1]) continue;
        const e: i32 = @as(i32, a.unk1E) - 0x80;
        if (e == h) return 3;
        if (e < h) {
            result = 2;
        } else if (result == 0) {
            result = 1;
        }
    }
    return result;
}
