//! zig_diff spec: func_800D7EF4 -> func_800D7EF4.func_800D7EF4 (6F014.c).
//! Inputs: the plgLoad state (0-3: 0 and 3 return 0, 1 falls through into 2),
//! the state-1 flag D_800F5684 (zero, or nonzero with and without the sign
//! bit), the load context (random lba and size), and the queue slot
//! D_800F568C (null, a queue whose state reads as Loaded, or one that does
//! not). The callees are recording stubs. The allocate stub copies the
//! CdFile the C fills (its lba and size) into the queue, so a wrong LBA
//! reaches the call trace and the arena. `_loadfile` is static and runs as C.
//! Assumptions to check against the DWARF: the queue's state is a u32 at
//! offset 0, and vs_main_CdQueueStateLoaded is a small value (0..7). The
//! loaded-state queues draw their state from 0..7, so the C's "loaded" test
//! is taken whenever that value matches the constant.
//! The switch has no default, so the state stays in 0..3: for any other
//! value ret is an indeterminate read that no port can match.
const std = @import("std");
const zd = @import("zdiff");
const psx = @import("common").psx;
const v = @import("views_6F014");
pub const Args = std.meta.ArgsTuple(@TypeOf(@import("port").func_800D7EF4));

pub const classes = [_][:0]const u8{
    "state0-or-3",
    "state1-flag-clear",
    "state1-flag-set",
    "state2-no-slot",
    "state2-loaded",
    "state2-loading",
};

/// The class of the input prepare built (set before classify runs).
var mode: usize = 0;

/// Assumed layout of the queue slot the C reads: state is a u32 at offset 0.
const CdQueue = extern struct { state: u32, rest: [28]u8 };

pub const stubs = struct {
    pub fn func_8007E180(ctx: *zd.Ctx, arg0: anytype) c_int {
        _ = arg0;
        return ctx.value(c_int);
    }

    pub fn vs_battle_setPlgLoadState(ctx: *zd.Ctx, state: anytype) void {
        _ = ctx;
        // The callee sets the load state the C's caller reads back.
        v.g._plgLoadState().* = @intCast(state);
    }

    pub fn vs_main_allocateCdQueueSlot(ctx: *zd.Ctx, cd: anytype) ?*anyopaque {
        if (ctx.value(u8) < 32) return null;
        const q = ctx.alloc(CdQueue);
        q.state = ctx.value(u32);
        // Keep the request the C wrote (lba and size): a wrong LBA shows in the arena.
        const src: *align(1) const [8]u8 = @ptrCast(cd);
        q.rest[0..8].* = src.*;
        return q;
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

pub fn prepare(ctx: *zd.Ctx) Args {
    const st = ctx.range(u32, 0, 3);
    v.g._plgLoadState().* = st;

    const flag: i32 = if (ctx.chance(2)) 0 else ctx.pick(i32, &.{ 1, -1 });
    v.g.D_800F5684().* = flag;

    v.g._plgLoadContext().* = .{ .lba = ctx.int(u16), .size = ctx.int(u16) };

    const slot = psx.global(?*anyopaque, "D_800F568C");
    if (st == 2) {
        if (ctx.chance(3)) {
            slot.* = null;
            mode = 3;
        } else if (ctx.chance(2)) {
            // Loaded: the state field is one of 0..7, so the constant is hit.
            const q = ctx.alloc(CdQueue);
            q.state = ctx.range(u32, 0, 7);
            slot.* = q;
            mode = 4;
        } else {
            const q = ctx.alloc(CdQueue);
            q.state = ctx.int(u32);
            slot.* = q;
            mode = 5;
        }
    } else {
        if (ctx.chance(2)) {
            slot.* = null;
        } else {
            const q = ctx.alloc(CdQueue);
            q.state = ctx.int(u32);
            slot.* = q;
        }
        mode = if (st == 1) (if (flag == 0) @as(usize, 1) else 2) else 0;
    }
    return .{};
}

pub fn classify(args: Args) usize {
    _ = args;
    return mode;
}
