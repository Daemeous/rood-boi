//! zig_diff spec: func_80099960 -> func_80099960.func_80099960 (30DB0.c).
//! The C copies 0xE0 bytes from arg0 into D_800F4448 through vs_main_memcpy.
//! Inputs: arg0 is a block in the arena, either 16-aligned or at an odd
//! offset (the two classes), filled with random bytes. The memcpy stub really
//! copies, so D_800F4448 and the call's arguments both reach the comparison.
//! The 0xE0 size and both pointer arguments are checked by the call trace.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_80099960));

pub const classes = [_][:0]const u8{ "aligned-src", "unaligned-src" };

pub const stubs = struct {
    pub fn vs_main_memcpy(ctx: *zd.Ctx, dest: anytype, src: anytype, sz: anytype) void {
        _ = ctx;
        const d: [*]u8 = @as([*]u8, @ptrCast(dest.?));
        const s: [*]const u8 = @as([*]const u8, @ptrCast(src.?));
        const n: usize = sz;
        @memcpy(d[0..n], s[0..n]);
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const unaligned = ctx.chance(2);
    const buf = ctx.allocBytes(0xE0 + 8, 16);
    const src: [*]u8 = if (unaligned) buf + 3 else buf;
    return .{@as(?*anyopaque, @ptrCast(src))};
}

pub fn classify(args: Args) usize {
    return if (@intFromPtr(args[0].?) & 15 != 0) 1 else 0;
}
