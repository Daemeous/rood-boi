//! zig_diff spec: func_8009A028 -> func_8009A028.func_8009A028 (30DB0.c).
//! Inputs: the destination is either null (the C allocates through
//! vs_main_allocHeap, a stub handing out a 256-byte arena block) or a 256-byte
//! arena block whose words 12..15 hold edge values (0, -1, INT_MAX, INT_MIN,
//! 1) or random bytes, so the additions wrap. The source is a separate 256-byte
//! arena block of random bytes (never aliasing the destination). The size is
//! picked from edges below and above the 64-byte line the C's four word
//! additions sit on, always within the 256-byte blocks. memcpy is a stub that
//! really copies n bytes, since the C's result depends on the copied bytes.
const std = @import("std");
const zd = @import("zdiff");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_8009A028));

pub const classes = [_][:0]const u8{ "alloc", "given-short", "given-long" };

fn asInts(p: [*]u8) [*c]c_int {
    return @ptrCast(@alignCast(p));
}

fn bytesOf(p: anytype) [*]u8 {
    return switch (@typeInfo(@TypeOf(p))) {
        .optional => @ptrCast(p.?),
        else => @ptrCast(p),
    };
}

fn constBytesOf(p: anytype) [*]const u8 {
    return switch (@typeInfo(@TypeOf(p))) {
        .optional => @ptrCast(p.?),
        else => @ptrCast(p),
    };
}

pub const stubs = struct {
    pub fn vs_main_allocHeap(ctx: *zd.Ctx, size: anytype) ?*anyopaque {
        _ = size;
        // Never null: the C writes through the result without a test.
        return @ptrCast(ctx.allocBytes(256, 16));
    }

    pub fn memcpy(ctx: *zd.Ctx, dst: anytype, src: anytype, n: anytype) ?*anyopaque {
        _ = ctx;
        const len: usize = if (n <= 0) 0 else @intCast(n);
        if (len != 0) {
            const d = bytesOf(dst);
            const s = constBytesOf(src);
            @memcpy(d[0..len], s[0..len]);
        }
        return null;
    }
};

pub fn prepare(ctx: *zd.Ctx) Args {
    const dst = ctx.allocBytes(256, 16);
    const src = ctx.allocBytes(256, 16);
    const words: [*]c_int = @ptrCast(@alignCast(dst));
    for (12..16) |i| {
        words[i] = ctx.pick(c_int, &.{ 0, -1, 0x7FFF_FFFF, -0x8000_0000, 1 });
    }
    const size = ctx.edge(c_int, &.{ 0, 1, 3, 4, 12, 16, 48, 60, 63, 64, 65, 127, 128, 255, 256 });
    const use_alloc = ctx.chance(4);
    const arg0: [*c]c_int = if (use_alloc) null else asInts(dst);
    return .{ arg0, @as(?*anyopaque, @ptrCast(src)), size };
}

pub fn classify(args: Args) usize {
    if (args[0] == null) return 0;
    return if (args[2] < 64) 1 else 2;
}
