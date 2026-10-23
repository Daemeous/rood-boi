//! zig_diff spec: func_800962E4 -> func_800962E4.func_800962E4 (2842C.c).
//! Inputs: a 40-element D_800F1BAC_t array in the arena, its base pointer
//! stored in the global with eight spare elements before element 0 so
//! negative indices stay in memory. The index D_800F227E is drawn from four
//! groups: in-range 0..31, exactly 32 (the ordered comparison's equal sides),
//! negative -8..-1 (signed read: 0xFFFF must not take the unsigned path), and
//! 33..0x7FFF (the shift path). The memcpy and bzero stubs really copy and
//! zero, so the arena contents after the shift are compared as well as the
//! call trace.
const std = @import("std");
const zd = @import("zdiff");
const v = @import("views_2842C");

pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800962E4));

pub const classes = [_][:0]const u8{ "in-range", "at-32", "negative", "above-32" };

pub const stubs = struct {
    pub fn vs_main_memcpy(ctx: *zd.Ctx, dest: anytype, src: anytype, sz: anytype) void {
        _ = ctx;
        const n: usize = @intCast(sz);
        const d: [*]u8 = @ptrCast(dest.?);
        const s: [*]const u8 = @ptrCast(src.?);
        for (0..n) |k| d[k] = s[k];
    }

    pub fn vs_main_bzero(ctx: *zd.Ctx, p: anytype, n: anytype) void {
        _ = ctx;
        const len: usize = @intCast(n);
        const d: [*]u8 = @ptrCast(p.?);
        @memset(d[0..len], 0);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const elem = @sizeOf(v.D_800F1BAC_t);
    // 40 elements: eight before element 0 serve negative indices.
    const block = ctx.allocBytes(40 * elem, 16);
    const base: [*]u8 = block + 8 * elem;
    v.g.D_800F1BAC().* = @ptrCast(base);

    const roll = ctx.range(u8, 0, 99);
    const index: i16 = if (roll < 40)
        ctx.range(i16, 0, 31)
    else if (roll < 55)
        ctx.edge(i16, &.{32})
    else if (roll < 70)
        ctx.range(i16, -8, -1)
    else
        ctx.range(i16, 33, 0x7FFF);
    v.g.D_800F227E().* = index;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    const i = v.g.D_800F227E().*;
    if (i < 0) return 2;
    if (i < 32) return 0;
    if (i == 32) return 1;
    return 3;
}
