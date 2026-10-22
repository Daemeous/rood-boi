//! zig_diff spec: vs_battle_loadEffPurge -> load_eff_purge.loadEffPurge (6F014.c).
//! Inputs: the one-shot flag D_800F5684 (zero or one of several nonzero
//! values, both sign bits), the CD queue pointer D_800F568C (null, or a
//! queue block whose first word is a small state value), and whether the
//! queue allocator fails. The allocator stub copies the first 8 bytes of
//! the CdFile it is given into the arena so the lba and size reach the
//! comparison, and the stubs return both null and valid pointers.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_6F014");
const port = @import("port");
pub const Args = std.meta.ArgsTuple(@TypeOf(port.loadEffPurge));

pub const classes = [_][:0]const u8{ "flag-clear", "alloc-null", "alloc-ok", "queued" };

/// Arena block the allocator stub copies the CdFile bytes into.
var sink: [*]u8 = undefined;
/// Arena block handed out as the CD queue slot.
var queue: [*]u8 = undefined;
/// Whether the allocator stub returns null this iteration.
var alloc_fails: bool = false;

fn queuePtr() *?*anyopaque {
    return psx.global(?*anyopaque, "D_800F568C");
}

pub fn prepare(ctx: *zd.Ctx) Args {
    sink = ctx.allocBytes(8, 8);
    queue = ctx.allocBytes(64, 16);
    // The queue's state word is assumed to sit at offset 0: small values
    // cover the Loaded state whatever its width.
    std.mem.writeInt(u32, queue[0..4], ctx.range(u32, 0, 7), .little);

    v.g.D_800F5684().* = if (ctx.chance(4))
        0
    else
        ctx.pick(i32, &.{ 1, -1, 2, std.math.maxInt(i32), std.math.minInt(i32) });

    alloc_fails = ctx.chance(3);
    const have = ctx.chance(2);
    const qp: ?*anyopaque = if (have) @ptrCast(queue) else null;
    queuePtr().* = qp;
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    if (v.g.D_800F5684().* == 0) return 0;
    if (queuePtr().* != null) return 3;
    return if (alloc_fails) 1 else 2;
}

pub const stubs = struct {
    pub fn func_8007E180(ctx: *zd.Ctx, arg0: anytype) c_int {
        _ = arg0;
        // The callee may change the one-shot flag; the C overwrites it after the call.
        v.g.D_800F5684().* = ctx.value(i32);
        return ctx.value(c_int);
    }

    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, file: anytype) ?*anyopaque {
        _ = ctx;
        // Record the lba and size the C passes, so a wrong constant shows.
        const src: [*]const u8 = @ptrCast(file);
        @memcpy(sink[0..8], src[0..8]);
        if (alloc_fails) return null;
        const p: *anyopaque = @ptrCast(queue);
        return p;
    }

    pub fn vs_main_cdEnqueue(ctx: *zd.Ctx, q: anytype, buf: anytype) void {
        _ = ctx;
        _ = q;
        _ = buf;
    }

    pub fn vs_main_freeCdQueueSlot(ctx: *zd.Ctx, q: anytype) void {
        _ = ctx;
        _ = q;
    }
};
