//! zig_diff spec: func_800D8400 -> func_800D8400.func_800D8400 (6FC00.c).
//! Inputs: a func_800D8400_t in the arena, disabled about one time in five
//! (nonzero values with and without the sign bit), with a random position.
//! `first`, `second` and `direction` are random 32-bit values, except that
//! the low 16 bits of every value the C uses as a table index stay below
//! 3092, so the table at 0x1F8003EC stays inside the scratchpad. Covered:
//! the equal-arguments single pass, first equal to the nearest direction
//! (which makes the second index load), direction edges at 512 and the
//! quadrant bits, and the callee results that are negative, zero and
//! positive. The stubs return each status the C tests (0, 1, 2, 3, 0x80)
//! and write the state fields the C reads back.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_6FC00");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D8400));

pub const classes = [_][:0]const u8{ "disabled", "equal", "first-is-nearest", "first-is-other" };

const small_edges = [_]u16{ 0, 1, 2, 3, 4, 5, 6, 7, 255, 256, 511, 512, 513, 1023, 3091 };

/// A table index the C may read: 0..3091 keeps 0x1F8003EC + index in the scratchpad.
fn smallIndex(ctx: *zd.Ctx) u16 {
    if (ctx.chance(2)) return small_edges[ctx.range(usize, 0, small_edges.len - 1)];
    return ctx.range(u16, 0, 3091);
}

/// A callee result: 0, a negative or a positive value.
fn signedResult(ctx: *zd.Ctx) c_int {
    const k = ctx.value(u3);
    if (k == 0) return 0;
    const n: c_int = @as(c_int, ctx.value(u16)) + 1;
    return if (k < 4) -n else n;
}

/// The C side passes its own declaration of the state type: view it as ours.
fn asState(p: anytype) *v.func_800D8400_t {
    return @ptrCast(@alignCast(p));
}

pub const stubs = struct {
    pub fn func_800D96C8(ctx: *zd.Ctx, state: anytype, source: anytype, dest: anytype, direction: anytype) c_int {
        _ = source;
        _ = dest;
        _ = direction;
        if (state) |p| {
            const s = asState(p);
            if (ctx.value(u2) == 0) s.destination.raw = ctx.value(u32);
        }
        return signedResult(ctx);
    }

    pub fn func_800D954C(ctx: *zd.Ctx, state: anytype, dest: anytype) c_int {
        _ = dest;
        if (state) |p| {
            const s = asState(p);
            if (ctx.value(u2) == 0) s.disabled = ctx.value(u8);
        }
        return signedResult(ctx);
    }

    pub fn func_800DB93C(ctx: *zd.Ctx, state: anytype, dest: anytype, direction: anytype, source: anytype) c_uint {
        _ = dest;
        _ = direction;
        _ = source;
        if (state) |p| {
            const s = asState(p);
            if (ctx.value(u2) == 0) s.position.raw = ctx.value(u32);
        }
        const status: u32 = switch (ctx.value(u3)) {
            0...2 => 0,
            3...4 => 1,
            5 => 2,
            6 => 3,
            else => 0x80,
        };
        return (ctx.value(u32) & 0x00FF_FFFF) | (status << 24);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const st = ctx.alloc(v.func_800D8400_t);
    st.disabled = if (ctx.chance(5)) ctx.pick(u8, &.{ 1, 0x80, 0xFF }) else 0;
    st.position.raw = ctx.int(u32);

    var direction: c_uint = ctx.pick(c_uint, &.{ 0, 0x1FF, 0x200, 0x201, 0x3FF, 0x400, 0x7FF, 0x800, 0xFFF, 0x1000, 0xFFFF_FFFF });
    var first: c_int = undefined;
    var second: c_int = undefined;
    if (ctx.chance(3)) {
        // Equal arguments: one pass, indexed by the low 16 bits of direction.
        first = @bitCast(ctx.int(c_uint));
        second = first;
        direction = (ctx.int(c_uint) & 0xFFFF_0000) | @as(c_uint, smallIndex(ctx));
    } else {
        const hi: u32 = @as(u32, ctx.int(u16)) << 16;
        first = @bitCast(hi | @as(u32, smallIndex(ctx)));
        // First equal to the nearest direction (0, 2, 4 or 6) sometimes: the second index is then read.
        if (ctx.chance(2)) first = @as(c_int, 2 * ctx.range(u8, 0, 3));
        const hi2: u32 = @as(u32, ctx.int(u16)) << 16;
        second = @bitCast(hi2 | @as(u32, smallIndex(ctx)));
        if (second == first) second ^= 1;
    }
    return .{ st, first, second, direction };
}

pub fn classify(args: Args) usize {
    if (args[0].?.disabled != 0) return 0;
    if (args[1] == args[2]) return 1;
    const dir: c_uint = args[3];
    const q = (dir & 0xFFF) >> 10;
    const nearest: c_uint = if ((dir & 0x3FF) > 512) ((q + 1) & 3) * 2 else q * 2;
    return if (@as(c_uint, @bitCast(args[1])) == nearest) 2 else 3;
}
