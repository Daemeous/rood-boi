//! zig_diff spec: func_800D55A4 -> func_800D55A4.func_800D55A4 (5BF94.c).
//! Inputs: a battle-data block (arg0) whose old heap block is null or not, and
//! a block3 entry whose count is an edge value (0, small positives, -1, -2,
//! -256, 256/257/511/1000, so the u8 `unk0` wraps). Each entry's tag byte is 3
//! about half the time, so the `== 3` test sees both outcomes. The stubs
//! return the entry, a valid heap block, and random write-backs for
//! func_800D5780. The C's `count` is a 0-based counter, so its sign bit is
//! never set and the sign-bit half of the zero-test value flag is structurally
//! unreachable.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_5BF94");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D55A4));

pub const classes = [_][:0]const u8{ "no-old-block", "empty-loop", "tag-3-seen", "no-tag-3" };

/// Room for 256 entries of 4 bytes after the count.
const entry_bytes = 4 + 4 * 256;

/// The block3 entry _getPFileBlock3section hands back (set by prepare).
var entry: *v.func_800D55A4_t = undefined;

pub const stubs = struct {
    pub fn _getPFileBlock3section(ctx: *zd.Ctx, arg0: anytype, arg1: anytype) ?*anyopaque {
        _ = ctx;
        _ = arg0;
        _ = arg1;
        return entry;
    }

    pub fn func_800D5564(ctx: *zd.Ctx, arg0: anytype, arg1: anytype, arg2: anytype) ?*anyopaque {
        _ = arg0;
        _ = arg1;
        _ = arg2;
        if (ctx.value(bool)) return null;
        return entry;
    }

    pub fn vs_main_freeHeapR(ctx: *zd.Ctx, p: anytype) void {
        _ = ctx;
        _ = p;
    }

    pub fn vs_main_allocHeapR(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        // Always a fixed 8 KB block: for a negative count the C's size is a
        // wrapped u_int, and the C writes at most 8 + 16 * 255 + 16 bytes.
        _ = size;
        return @ptrCast(ctx.allocBytes(8192, 16));
    }

    pub fn func_800D5780(ctx: *zd.Ctx, p: anytype) c_int {
        // The callee may rewrite the element it is given.
        const e: *v.func_800D5780_t = @ptrCast(@alignCast(p.?));
        if (ctx.value(u1) == 1) e.unk6 = ctx.value(i16);
        if (ctx.value(u1) == 1) e.unkC = ctx.value(i32);
        return ctx.value(c_int);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const holder = ctx.alloc(v.D_800F569C_t);
    const block3 = ctx.alloc(v.pFileBlock3);
    psx.put(&holder.block3Data, block3);
    v.g.D_800F569C().* = holder;

    entry = @ptrCast(@alignCast(ctx.allocBytes(entry_bytes, 16)));
    const count = ctx.edge(i32, &.{ 0, 1, 2, 3, 8, -1, -2, -256, 256, 257, 511, 1000 });
    entry.count = count;
    const bytes: [*]u8 = @ptrCast(entry);
    for (0..256) |i| {
        bytes[4 + 4 * i] = if (ctx.chance(2)) 3 else ctx.pick(u8, &.{ 0, 1, 2, 4, 255 });
    }

    const arg0 = ctx.alloc(v.D_800F53B8_t);
    if (ctx.chance(2)) {
        psx.put(&arg0.unkD1C.unk30, null);
    } else {
        const old: *v.D_800F53B8_t4 = @ptrCast(@alignCast(ctx.allocBytes(64, 16)));
        psx.put(&arg0.unkD1C.unk30, old);
    }
    return .{ arg0, ctx.pick(c_int, &.{ 0, 1, -1, 2 }), ctx.int(c_int) };
}

pub fn classify(args: Args) usize {
    if (psx.get(args[0].?.unkD1C.unk30) == null) return 0;
    const n: u8 = @truncate(@as(u32, @bitCast(entry.count)));
    if (n == 0) return 1;
    const count: usize = n;
    const bytes: [*]u8 = @ptrCast(entry);
    for (0..count) |i| {
        if (bytes[4 + 4 * i] == 3) return 2;
    }
    return 3;
}
